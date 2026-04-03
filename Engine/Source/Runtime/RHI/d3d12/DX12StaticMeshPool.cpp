#include "DX12StaticMeshPool.h"

// === C++ includes ===
#include <cstring>

namespace Cue::RHI::DX12
{
    namespace
    {
        [[nodiscard]] uint64_t align_up_u64(uint64_t value, uint32_t alignment) noexcept
        {
            if (alignment <= 1)
            {
                return value;
            }

            const uint64_t remainder = value % static_cast<uint64_t>(alignment);
            return remainder == 0 ? value : (value + (static_cast<uint64_t>(alignment) - remainder));
        }

        [[nodiscard]] bool allocate_from_free_ranges(
            std::vector<FreeRange>& freeRanges,
            uint64_t byteSize,
            uint32_t alignment,
            uint64_t& outOffset) noexcept
        {
            outOffset = 0;
            for (size_t i = 0; i < freeRanges.size(); ++i)
            {
                const FreeRange freeRange = freeRanges[i];
                const uint64_t alignedOffset = align_up_u64(freeRange.byteOffset, alignment);
                const uint64_t prefixBytes = alignedOffset - freeRange.byteOffset;
                if (prefixBytes + byteSize > freeRange.byteSize)
                {
                    continue;
                }

                const uint64_t suffixOffset = alignedOffset + byteSize;
                const uint64_t suffixBytes = freeRange.byteSize - prefixBytes - byteSize;

                if (prefixBytes > 0 && suffixBytes > 0)
                {
                    freeRanges[i].byteSize = prefixBytes;
                    freeRanges.insert(
                        freeRanges.begin() + static_cast<std::ptrdiff_t>(i + 1),
                        FreeRange{ suffixOffset, suffixBytes });
                }
                else if (prefixBytes > 0)
                {
                    freeRanges[i].byteSize = prefixBytes;
                }
                else if (suffixBytes > 0)
                {
                    freeRanges[i].byteOffset = suffixOffset;
                    freeRanges[i].byteSize = suffixBytes;
                }
                else
                {
                    freeRanges.erase(freeRanges.begin() + static_cast<std::ptrdiff_t>(i));
                }

                outOffset = alignedOffset;
                return true;
            }

            return false;
        }

        void release_to_free_ranges(
            std::vector<FreeRange>& freeRanges,
            uint64_t byteOffset,
            uint64_t byteSize) noexcept
        {
            if (byteSize == 0)
            {
                return;
            }

            auto insertIt = freeRanges.begin();
            while (insertIt != freeRanges.end() && insertIt->byteOffset < byteOffset)
            {
                ++insertIt;
            }
            insertIt = freeRanges.insert(insertIt, FreeRange{ byteOffset, byteSize });

            if (insertIt != freeRanges.begin())
            {
                auto prevIt = insertIt;
                --prevIt;
                if (prevIt->byteOffset + prevIt->byteSize == insertIt->byteOffset)
                {
                    prevIt->byteSize += insertIt->byteSize;
                    insertIt = freeRanges.erase(insertIt);
                    insertIt = prevIt;
                }
            }

            auto nextIt = insertIt;
            ++nextIt;
            if (nextIt != freeRanges.end() && insertIt->byteOffset + insertIt->byteSize == nextIt->byteOffset)
            {
                insertIt->byteSize += nextIt->byteSize;
                freeRanges.erase(nextIt);
            }
        }

        template<typename T>
        [[nodiscard]] uint64_t byte_size_of(const std::vector<T>& values) noexcept
        {
            return static_cast<uint64_t>(values.size()) * static_cast<uint64_t>(sizeof(T));
        }
    }

    DX12StaticMeshPool::DX12StaticMeshPool(
        const StaticMeshPoolDesc& desc,
        DX12BufferManager& bufferManager,
        DX12ViewManager& viewManager,
        DX12CommandPool& commandPool,
        DX12QueuePool& queuePool)
        : m_bufferManager(bufferManager)
        , m_viewManager(viewManager)
        , m_commandPool(commandPool)
        , m_queuePool(queuePool)
    {
        // 1) コンストラクタでは初期化結果だけ保持し、呼び出し側は allocate_mesh で検査できるようにする。
        m_initResult = initialize_streams(desc);
        if (m_initResult)
        {
            m_initResult = initialize_mesh_range_state(desc);
        }
    }

    DX12StaticMeshPool::~DX12StaticMeshPool()
    {
        // 1) 生成順の逆順で破棄し、upload/default の両方を BufferManager へ戻す。
        if (m_meshRangeState.srvHandle.valid())
        {
            m_viewManager.destroy_view(m_meshRangeState.srvHandle);
            m_meshRangeState.srvHandle = {};
        }
        if (m_meshRangeState.uploadBufferHandle.valid())
        {
            m_bufferManager.destroy_buffer(m_meshRangeState.uploadBufferHandle);
            m_meshRangeState.uploadBufferHandle = {};
        }
        if (m_meshRangeState.defaultBufferHandle.valid())
        {
            m_bufferManager.destroy_buffer(m_meshRangeState.defaultBufferHandle);
            m_meshRangeState.defaultBufferHandle = {};
        }
        m_meshRangeState.mappedUploadData = nullptr;
        m_meshRangeState.capacity = 0;
        m_meshRangeState.freeMeshIds.clear();
        destroy_stream_state(m_indexStream);
        destroy_stream_state(m_normalStream);
        destroy_stream_state(m_uvStream);
        destroy_stream_state(m_positionStream);
    }

    Result DX12StaticMeshPool::initialize_streams(const StaticMeshPoolDesc& desc)
    {
        // 1) 各ストリームごとの総容量を確定し、永続 default と staging upload を作る。
        Result result = create_stream_state(
            desc.positionBufferName,
            BufferType::Vertex,
            static_cast<uint64_t>(desc.maxVertexCount) * sizeof(Math::float4),
            sizeof(Math::float4),
            desc.maxVertexCount,
            alignof(Math::float4),
            m_positionStream);
        if (!result)
        {
            return result;
        }

        result = create_stream_state(
            desc.uvBufferName,
            BufferType::Vertex,
            static_cast<uint64_t>(desc.maxVertexCount) * sizeof(Math::float2),
            sizeof(Math::float2),
            desc.maxVertexCount,
            alignof(Math::float2),
            m_uvStream);
        if (!result)
        {
            return result;
        }

        result = create_stream_state(
            desc.normalBufferName,
            BufferType::Vertex,
            static_cast<uint64_t>(desc.maxVertexCount) * sizeof(Math::float3),
            sizeof(Math::float3),
            desc.maxVertexCount,
            alignof(Math::float3),
            m_normalStream);
        if (!result)
        {
            return result;
        }

        result = create_stream_state(
            desc.indexBufferName,
            BufferType::Index,
            static_cast<uint64_t>(desc.maxIndexCount) * sizeof(uint32_t),
            sizeof(uint32_t),
            desc.maxIndexCount,
            alignof(uint32_t),
            m_indexStream);
        if (!result)
        {
            return result;
        }

        return Result::ok();
    }
    Result DX12StaticMeshPool::initialize_mesh_range_state(const StaticMeshPoolDesc& desc)
    {
        BufferDesc defaultDesc{};
        defaultDesc.name = desc.meshRangeBufferName;
        defaultDesc.type = BufferType::Structured;
        defaultDesc.defaultHeapCount = 1;
        defaultDesc.uploadHeapCount = 0;
        defaultDesc.initialState = ResourceState::Common;
        defaultDesc.stride = sizeof(StaticMeshRange);
        defaultDesc.elementCount = desc.maxMeshCount;
        defaultDesc.size = defaultDesc.stride * defaultDesc.elementCount;
        defaultDesc.alignment = alignof(StaticMeshRange);

        Result result = m_bufferManager.create_buffer(defaultDesc, m_meshRangeState.defaultBufferHandle);
        if (!result)
        {
            return result;
        }

        BufferDesc uploadDesc{};
        uploadDesc.name = std::string(desc.meshRangeBufferName) + ".Upload";
        uploadDesc.type = BufferType::Structured;
        uploadDesc.defaultHeapCount = 0;
        uploadDesc.uploadHeapCount = 1;
        uploadDesc.initialState = ResourceState::CopySource;
        uploadDesc.stride = sizeof(StaticMeshRange);
        uploadDesc.elementCount = desc.maxMeshCount;
        uploadDesc.size = uploadDesc.stride * uploadDesc.elementCount;
        uploadDesc.alignment = alignof(StaticMeshRange);

        result = m_bufferManager.create_buffer(uploadDesc, m_meshRangeState.uploadBufferHandle);
        if (!result)
        {
            m_bufferManager.destroy_buffer(m_meshRangeState.defaultBufferHandle);
            m_meshRangeState.defaultBufferHandle = {};
            return result;
        }

        UploadBufferView uploadView{};
        result = m_bufferManager.get_upload_buffer_view(m_meshRangeState.uploadBufferHandle, uploadView);
        if (!result)
        {
            return result;
        }
        if (uploadView.mappedDatas.size() != 1 || uploadView.mappedDatas[0] == nullptr)
        {
            return Result::fail(
                Code::InternalError,
                Severity::Error,
                "Static mesh range upload buffer is not mapped.");
        }

        ViewDesc meshRangeSrvDesc{};
        meshRangeSrvDesc.name = desc.meshRangeViewName;
        meshRangeSrvDesc.type = ViewType::ShaderResourceBuffer;
        meshRangeSrvDesc.bufferKind = BufferKind::Buffer;
        meshRangeSrvDesc.bufferHandle = m_meshRangeState.defaultBufferHandle;
        meshRangeSrvDesc.firstElement = 0;
        meshRangeSrvDesc.numElements = desc.maxMeshCount;
        meshRangeSrvDesc.structureByteStride = sizeof(StaticMeshRange);
        result = m_viewManager.create_view(meshRangeSrvDesc, m_meshRangeState.srvHandle);
        if (!result)
        {
            return result;
        }

        m_meshRangeState.mappedUploadData = uploadView.mappedDatas[0];
        m_meshRangeState.capacity = desc.maxMeshCount;
        std::memset(m_meshRangeState.mappedUploadData, 0, defaultDesc.size);
        m_meshRangeState.freeMeshIds.clear();
        m_meshRangeState.freeMeshIds.reserve(desc.maxMeshCount);
        for (uint32_t meshId = desc.maxMeshCount; meshId > 0; --meshId)
        {
            m_meshRangeState.freeMeshIds.push_back(meshId - 1);
        }

        BufferCopyRegion initializeRegion{};
        initializeRegion.srcBufferHandle = m_meshRangeState.uploadBufferHandle;
        initializeRegion.srcUploadResourceIndex = 0;
        initializeRegion.srcByteOffset = 0;
        initializeRegion.dstBufferHandle = m_meshRangeState.defaultBufferHandle;
        initializeRegion.dstDefaultResourceIndex = 0;
        initializeRegion.dstByteOffset = 0;
        initializeRegion.byteSize = defaultDesc.size;
        std::vector<BufferCopyRegion> initializeRegions{ initializeRegion };
        result = copy_upload_regions(initializeRegions);
        if (!result)
        {
            return result;
        }

        return Result::ok();
    }

    Result DX12StaticMeshPool::create_stream_state(
        std::string_view bufferName,
        BufferType bufferType,
        uint64_t totalBytes,
        uint32_t stride,
        uint32_t elementCount,
        uint32_t alignment,
        StreamState& outStreamState)
    {
        // 1) 常駐先は default heap、コピー元 staging は upload heap として別バッファを作る。
        BufferDesc defaultDesc{};
        defaultDesc.name = bufferName;
        defaultDesc.type = bufferType;
        defaultDesc.defaultHeapCount = 1;
        defaultDesc.uploadHeapCount = 0;
        defaultDesc.initialState = ResourceState::Common;
        defaultDesc.stride = stride;
        defaultDesc.elementCount = elementCount;
        defaultDesc.size = static_cast<uint32_t>(totalBytes);
        defaultDesc.alignment = alignment;

        Result result = m_bufferManager.create_buffer(defaultDesc, outStreamState.defaultBufferHandle);
        if (!result)
        {
            return result;
        }

        std::string uploadName = std::string(bufferName) + ".Upload";
        BufferDesc uploadDesc{};
        uploadDesc.name = uploadName;
        uploadDesc.type = bufferType;
        uploadDesc.defaultHeapCount = 0;
        uploadDesc.uploadHeapCount = 1;
        uploadDesc.initialState = ResourceState::CopySource;
        uploadDesc.stride = stride;
        uploadDesc.elementCount = elementCount;
        uploadDesc.size = static_cast<uint32_t>(totalBytes);
        uploadDesc.alignment = alignment;

        result = m_bufferManager.create_buffer(uploadDesc, outStreamState.uploadBufferHandle);
        if (!result)
        {
            m_bufferManager.destroy_buffer(outStreamState.defaultBufferHandle);
            outStreamState.defaultBufferHandle = {};
            return result;
        }

        // 2) upload 側の永続 map を掴み、ring staging と free-list を初期化する。
        UploadBufferView uploadView{};
        result = m_bufferManager.get_upload_buffer_view(outStreamState.uploadBufferHandle, uploadView);
        if (!result)
        {
            destroy_stream_state(outStreamState);
            return result;
        }
        if (uploadView.mappedDatas.size() != 1 || uploadView.mappedDatas[0] == nullptr)
        {
            destroy_stream_state(outStreamState);
            return Result::fail(
                Code::InternalError,
                Severity::Error,
                "Static mesh pool upload buffer is not mapped.");
        }

        outStreamState.capacityInBytes = totalBytes;
        outStreamState.alignment = alignment;
        outStreamState.mappedUploadData = uploadView.mappedDatas[0];
        outStreamState.freeRanges.clear();
        outStreamState.freeRanges.push_back(FreeRange{ 0, totalBytes });
        outStreamState.uploadRing.initialize(static_cast<size_t>(totalBytes));
        return Result::ok();
    }

    Result DX12StaticMeshPool::allocate_stream_range(
        StreamState& streamState,
        uint64_t byteSize,
        uint32_t alignment,
        uint64_t& outOffset)
    {
        // 1) 常駐先は free-list で管理し、任意順の解放後でも再利用できるようにする。
        if (!allocate_from_free_ranges(streamState.freeRanges, byteSize, alignment, outOffset))
        {
            return Result::fail(
                Code::OutOfMemory,
                Severity::Error,
                "Static mesh pool default buffer is out of space.");
        }

        return Result::ok();
    }

    void DX12StaticMeshPool::release_stream_range(
        StreamState& streamState,
        uint64_t byteOffset,
        uint64_t byteSize)
    {
        // 1) 解放済み領域を free-list に戻し、隣接区間は即時マージして断片化を抑える。
        release_to_free_ranges(streamState.freeRanges, byteOffset, byteSize);
    }

    Result DX12StaticMeshPool::allocate_upload_range(
        StreamState& streamState,
        uint64_t byteSize,
        uint32_t alignment,
        Core::RingBuffer::Allocation& outAllocation)
    {
        // 1) staging は FIFO 再利用前提なので ring buffer からだけ確保する。
        if (!streamState.uploadRing.allocate(
            static_cast<size_t>(byteSize),
            static_cast<size_t>(alignment),
            outAllocation))
        {
            return Result::fail(
                Code::OutOfMemory,
                Severity::Error,
                "Static mesh pool upload ring buffer is out of space.");
        }

        return Result::ok();
    }

    void DX12StaticMeshPool::release_upload_range(
        StreamState& streamState,
        const Core::RingBuffer::Allocation& allocation)
    {
        // 1) 同期 upload 完了後だけ解放し、staging 領域の上書きを防ぐ。
        if (allocation.valid())
        {
            const bool released = streamState.uploadRing.release(allocation);
            CUE_ASSERT_MSG(released, "Failed to release static mesh upload ring allocation.");
        }
    }

    void DX12StaticMeshPool::write_upload_bytes(
        StreamState& streamState,
        const Core::RingBuffer::Allocation& allocation,
        const void* sourceData,
        uint64_t byteSize)
    {
        // 1) upload staging へ直接書き込み、データ未指定の属性はゼロで埋める。
        std::byte* dst = streamState.mappedUploadData + allocation.offset;
        if (sourceData == nullptr)
        {
            std::memset(dst, 0, static_cast<size_t>(byteSize));
            return;
        }

        std::memcpy(dst, sourceData, static_cast<size_t>(byteSize));
    }

    Result DX12StaticMeshPool::copy_upload_regions(const std::vector<BufferCopyRegion>& regions)
    {
        if (regions.empty())
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Copy regions must not be empty.");
        }

        CommandContextLease commandContext{};
        Result result = m_commandPool.get_command_context(CommandListType::Copy, commandContext);
        if (!result)
        {
            return result;
        }

        QueueContextLease queueContext{};
        result = m_queuePool.get_queue_context(CommandListType::Copy, queueContext);
        if (!result)
        {
            m_commandPool.return_command_context(commandContext);
            return result;
        }

        result = commandContext->reset();
        if (result)
        {
            result = commandContext->setup(0, 1);
        }

        if (result)
        {
            for (const BufferCopyRegion& region : regions)
            {
                ResourceBarrierDesc toCopyDestBarrier{};
                toCopyDestBarrier.after = ResourceState::CopyDest;
                result = commandContext->resource_barrier(region.dstBufferHandle, toCopyDestBarrier);
                if (!result)
                {
                    break;
                }

                result = commandContext->copy_buffer_region(region);
                ResourceBarrierDesc toCommonBarrier{};
                toCommonBarrier.after = ResourceState::Common;
                Result restoreResult = commandContext->resource_barrier(region.dstBufferHandle, toCommonBarrier);
                if (!result)
                {
                    break;
                }
                if (!restoreResult)
                {
                    result = restoreResult;
                    break;
                }
            }
        }

        if (result)
        {
            result = commandContext->close();
        }
        if (result)
        {
            std::vector<ICommandContext*> contexts{ commandContext.get() };
            result = queueContext->submit(contexts);
        }
        if (result)
        {
            result = queueContext->signal();
        }
        if (result)
        {
            result = queueContext->wait();
        }

        m_commandPool.return_command_context(commandContext);
        m_queuePool.return_queue_context(queueContext);
        return result;
    }

    void DX12StaticMeshPool::destroy_stream_state(StreamState& streamState)
    {
        // 1) upload/default の順に破棄し、BufferManager の所有実体を明示的に返す。
        if (streamState.uploadBufferHandle.valid())
        {
            m_bufferManager.destroy_buffer(streamState.uploadBufferHandle);
            streamState.uploadBufferHandle = {};
        }
        if (streamState.defaultBufferHandle.valid())
        {
            m_bufferManager.destroy_buffer(streamState.defaultBufferHandle);
            streamState.defaultBufferHandle = {};
        }

        streamState.freeRanges.clear();
        streamState.uploadRing.clear();
        streamState.mappedUploadData = nullptr;
        streamState.capacityInBytes = 0;
    }
    Result DX12StaticMeshPool::allocate_mesh_id(uint32_t& outMeshId)
    {
        if (m_meshRangeState.freeMeshIds.empty())
        {
            return Result::fail(
                Code::OutOfMemory,
                Severity::Error,
                "Static mesh range buffer is out of mesh slots.");
        }

        outMeshId = m_meshRangeState.freeMeshIds.back();
        m_meshRangeState.freeMeshIds.pop_back();
        return Result::ok();
    }
    void DX12StaticMeshPool::release_mesh_id(uint32_t meshId)
    {
        if (meshId >= m_meshRangeState.capacity)
        {
            return;
        }

        m_meshRangeState.freeMeshIds.push_back(meshId);
    }
    void DX12StaticMeshPool::write_mesh_range(uint32_t meshId, const StaticMeshRange& meshRange)
    {
        if (m_meshRangeState.mappedUploadData == nullptr || meshId >= m_meshRangeState.capacity)
        {
            return;
        }

        const uint64_t byteOffset = static_cast<uint64_t>(meshId) * sizeof(StaticMeshRange);
        std::memcpy(m_meshRangeState.mappedUploadData + byteOffset, &meshRange, sizeof(StaticMeshRange));
    }
    Result DX12StaticMeshPool::upload_mesh_range(uint32_t meshId, const StaticMeshRange& meshRange)
    {
        write_mesh_range(meshId, meshRange);

        BufferCopyRegion region{};
        region.srcBufferHandle = m_meshRangeState.uploadBufferHandle;
        region.srcUploadResourceIndex = 0;
        region.srcByteOffset = static_cast<uint64_t>(meshId) * sizeof(StaticMeshRange);
        region.dstBufferHandle = m_meshRangeState.defaultBufferHandle;
        region.dstDefaultResourceIndex = 0;
        region.dstByteOffset = region.srcByteOffset;
        region.byteSize = sizeof(StaticMeshRange);

        std::vector<BufferCopyRegion> regions{ region };
        return copy_upload_regions(regions);
    }

    Result DX12StaticMeshPool::allocate_mesh(const Core::Native::MeshData& meshData, StaticMeshHandle& outHandle)
    {
        // 1) 初期化状態と入力データを検証し、壊れた pool での割り当てを防ぐ。
        outHandle = {};
        if (!m_initResult)
        {
            return m_initResult;
        }
        if (meshData.positions.empty() || meshData.indices.empty())
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "MeshData must contain positions and indices.");
        }

        const uint32_t vertexCount = meshData.vertex_count();
        if (!meshData.uvs.empty() && meshData.uvs.size() != meshData.positions.size())
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "UV count must match the position count.");
        }
        if (!meshData.normals.empty() && meshData.normals.size() != meshData.positions.size())
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Normal count must match the position count.");
        }

        // 2) 常駐先の空き領域を先に押さえ、どれか一つでも足りなければ全体を巻き戻す。
        StaticMeshRecord record{};
        record.vertexCount = vertexCount;
        record.indexCount = static_cast<uint32_t>(meshData.indices.size());
        record.positionByteSize = byte_size_of(meshData.positions);
        record.uvByteSize = static_cast<uint64_t>(vertexCount) * sizeof(Math::float2);
        record.normalByteSize = static_cast<uint64_t>(vertexCount) * sizeof(Math::float3);
        record.indexByteSize = byte_size_of(meshData.indices);
        Result result = allocate_mesh_id(record.meshId);
        if (!result)
        {
            return result;
        }

        result = allocate_stream_range(
            m_positionStream,
            record.positionByteSize,
            alignof(Math::float4),
            record.positionByteOffset);
        if (!result)
        {
            release_mesh_id(record.meshId);
            return result;
        }

        result = allocate_stream_range(
            m_uvStream,
            record.uvByteSize,
            alignof(Math::float2),
            record.uvByteOffset);
        if (!result)
        {
            release_stream_range(m_positionStream, record.positionByteOffset, record.positionByteSize);
            release_mesh_id(record.meshId);
            return result;
        }

        result = allocate_stream_range(
            m_normalStream,
            record.normalByteSize,
            alignof(Math::float3),
            record.normalByteOffset);
        if (!result)
        {
            release_stream_range(m_uvStream, record.uvByteOffset, record.uvByteSize);
            release_stream_range(m_positionStream, record.positionByteOffset, record.positionByteSize);
            release_mesh_id(record.meshId);
            return result;
        }

        result = allocate_stream_range(
            m_indexStream,
            record.indexByteSize,
            alignof(uint32_t),
            record.indexByteOffset);
        if (!result)
        {
            release_stream_range(m_normalStream, record.normalByteOffset, record.normalByteSize);
            release_stream_range(m_uvStream, record.uvByteOffset, record.uvByteSize);
            release_stream_range(m_positionStream, record.positionByteOffset, record.positionByteSize);
            release_mesh_id(record.meshId);
            return result;
        }

        // 3) staging ring を確保して CPU データを書き込み、1 回の submit にまとめて転送する。
        Core::RingBuffer::Allocation positionUpload{};
        Core::RingBuffer::Allocation uvUpload{};
        Core::RingBuffer::Allocation normalUpload{};
        Core::RingBuffer::Allocation indexUpload{};

        result = allocate_upload_range(m_positionStream, record.positionByteSize, alignof(Math::float4), positionUpload);
        if (!result)
        {
            release_stream_range(m_indexStream, record.indexByteOffset, record.indexByteSize);
            release_stream_range(m_normalStream, record.normalByteOffset, record.normalByteSize);
            release_stream_range(m_uvStream, record.uvByteOffset, record.uvByteSize);
            release_stream_range(m_positionStream, record.positionByteOffset, record.positionByteSize);
            release_mesh_id(record.meshId);
            return result;
        }

        result = allocate_upload_range(m_uvStream, record.uvByteSize, alignof(Math::float2), uvUpload);
        if (!result)
        {
            release_upload_range(m_positionStream, positionUpload);
            release_stream_range(m_indexStream, record.indexByteOffset, record.indexByteSize);
            release_stream_range(m_normalStream, record.normalByteOffset, record.normalByteSize);
            release_stream_range(m_uvStream, record.uvByteOffset, record.uvByteSize);
            release_stream_range(m_positionStream, record.positionByteOffset, record.positionByteSize);
            release_mesh_id(record.meshId);
            return result;
        }

        result = allocate_upload_range(m_normalStream, record.normalByteSize, alignof(Math::float3), normalUpload);
        if (!result)
        {
            release_upload_range(m_uvStream, uvUpload);
            release_upload_range(m_positionStream, positionUpload);
            release_stream_range(m_indexStream, record.indexByteOffset, record.indexByteSize);
            release_stream_range(m_normalStream, record.normalByteOffset, record.normalByteSize);
            release_stream_range(m_uvStream, record.uvByteOffset, record.uvByteSize);
            release_stream_range(m_positionStream, record.positionByteOffset, record.positionByteSize);
            release_mesh_id(record.meshId);
            return result;
        }

        result = allocate_upload_range(m_indexStream, record.indexByteSize, alignof(uint32_t), indexUpload);
        if (!result)
        {
            release_upload_range(m_normalStream, normalUpload);
            release_upload_range(m_uvStream, uvUpload);
            release_upload_range(m_positionStream, positionUpload);
            release_stream_range(m_indexStream, record.indexByteOffset, record.indexByteSize);
            release_stream_range(m_normalStream, record.normalByteOffset, record.normalByteSize);
            release_stream_range(m_uvStream, record.uvByteOffset, record.uvByteSize);
            release_stream_range(m_positionStream, record.positionByteOffset, record.positionByteSize);
            release_mesh_id(record.meshId);
            return result;
        }

        write_upload_bytes(m_positionStream, positionUpload, meshData.positions.data(), record.positionByteSize);
        write_upload_bytes(
            m_uvStream,
            uvUpload,
            meshData.uvs.empty() ? nullptr : meshData.uvs.data(),
            record.uvByteSize);
        write_upload_bytes(
            m_normalStream,
            normalUpload,
            meshData.normals.empty() ? nullptr : meshData.normals.data(),
            record.normalByteSize);
        write_upload_bytes(m_indexStream, indexUpload, meshData.indices.data(), record.indexByteSize);

        StaticMeshRange meshRange{};
        meshRange.indexCount = record.indexCount;
        meshRange.startIndex = static_cast<uint32_t>(record.indexByteOffset / sizeof(uint32_t));
        meshRange.baseVertex = static_cast<int32_t>(record.positionByteOffset / sizeof(Math::float4));
        write_mesh_range(record.meshId, meshRange);

        std::vector<BufferCopyRegion> uploadRegions
        {
            BufferCopyRegion
            {
                .srcBufferHandle = m_positionStream.uploadBufferHandle,
                .srcUploadResourceIndex = 0,
                .srcByteOffset = positionUpload.offset,
                .dstBufferHandle = m_positionStream.defaultBufferHandle,
                .dstDefaultResourceIndex = 0,
                .dstByteOffset = record.positionByteOffset,
                .byteSize = record.positionByteSize
            },
            BufferCopyRegion
            {
                .srcBufferHandle = m_uvStream.uploadBufferHandle,
                .srcUploadResourceIndex = 0,
                .srcByteOffset = uvUpload.offset,
                .dstBufferHandle = m_uvStream.defaultBufferHandle,
                .dstDefaultResourceIndex = 0,
                .dstByteOffset = record.uvByteOffset,
                .byteSize = record.uvByteSize
            },
            BufferCopyRegion
            {
                .srcBufferHandle = m_normalStream.uploadBufferHandle,
                .srcUploadResourceIndex = 0,
                .srcByteOffset = normalUpload.offset,
                .dstBufferHandle = m_normalStream.defaultBufferHandle,
                .dstDefaultResourceIndex = 0,
                .dstByteOffset = record.normalByteOffset,
                .byteSize = record.normalByteSize
            },
            BufferCopyRegion
            {
                .srcBufferHandle = m_indexStream.uploadBufferHandle,
                .srcUploadResourceIndex = 0,
                .srcByteOffset = indexUpload.offset,
                .dstBufferHandle = m_indexStream.defaultBufferHandle,
                .dstDefaultResourceIndex = 0,
                .dstByteOffset = record.indexByteOffset,
                .byteSize = record.indexByteSize
            },
            BufferCopyRegion
            {
                .srcBufferHandle = m_meshRangeState.uploadBufferHandle,
                .srcUploadResourceIndex = 0,
                .srcByteOffset = static_cast<uint64_t>(record.meshId) * sizeof(StaticMeshRange),
                .dstBufferHandle = m_meshRangeState.defaultBufferHandle,
                .dstDefaultResourceIndex = 0,
                .dstByteOffset = static_cast<uint64_t>(record.meshId) * sizeof(StaticMeshRange),
                .byteSize = sizeof(StaticMeshRange)
            }
        };

        result = copy_upload_regions(uploadRegions);

        release_upload_range(m_indexStream, indexUpload);
        release_upload_range(m_normalStream, normalUpload);
        release_upload_range(m_uvStream, uvUpload);
        release_upload_range(m_positionStream, positionUpload);

        if (!result)
        {
            release_stream_range(m_indexStream, record.indexByteOffset, record.indexByteSize);
            release_stream_range(m_normalStream, record.normalByteOffset, record.normalByteSize);
            release_stream_range(m_uvStream, record.uvByteOffset, record.uvByteSize);
            release_stream_range(m_positionStream, record.positionByteOffset, record.positionByteSize);
            release_mesh_id(record.meshId);
            return result;
        }

        // 4) 転送完了後にメッシュレコードを登録し、必要なら名前引きも更新する。
        if (!meshData.name.empty())
        {
            record.nameId = Core::fnv1a64(meshData.name);
        }

        StaticMeshHandle handle = m_meshRegistry.create(record);
        if (record.nameId != 0)
        {
            m_nameToHandlesMap[record.nameId] = handle;
        }

        outHandle = handle;
        return Result::ok();
    }

    Result DX12StaticMeshPool::free_mesh(StaticMeshHandle handle)
    {
        // 1) レコードを解決して、常駐領域と名前引きをまとめて巻き戻す。
        StaticMeshRecord record{};
        if (!m_meshRegistry.try_copy_get(handle, record))
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Static mesh handle was not found.");
        }

        release_stream_range(m_positionStream, record.positionByteOffset, record.positionByteSize);
        release_stream_range(m_uvStream, record.uvByteOffset, record.uvByteSize);
        release_stream_range(m_normalStream, record.normalByteOffset, record.normalByteSize);
        release_stream_range(m_indexStream, record.indexByteOffset, record.indexByteSize);
        release_mesh_id(record.meshId);

        Result result = upload_mesh_range(record.meshId, StaticMeshRange{});
        if (!result)
        {
            return result;
        }

        if (record.nameId != 0)
        {
            const auto it = m_nameToHandlesMap.find(record.nameId);
            if (it != m_nameToHandlesMap.end() && it->second == handle)
            {
                m_nameToHandlesMap.erase(it);
            }
        }

        // 2) registry から外してハンドルを無効化し、次回の再利用に備える。
        if (!m_meshRegistry.destroy(handle))
        {
            return Result::fail(
                Code::InternalError,
                Severity::Error,
                "Failed to destroy static mesh record.");
        }

        return Result::ok();
    }
    Result DX12StaticMeshPool::get_mesh_id(StaticMeshHandle handle, uint32_t& outMeshId) const
    {
        StaticMeshRecord record{};
        if (!m_meshRegistry.try_copy_get(handle, record))
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Static mesh handle was not found.");
        }

        outMeshId = record.meshId;
        return Result::ok();
    }
    Result DX12StaticMeshPool::get_bindings(StaticMeshPoolBindings& outBindings) const
    {
        outBindings.positionBuffer = m_positionStream.defaultBufferHandle;
        outBindings.uvBuffer = m_uvStream.defaultBufferHandle;
        outBindings.normalBuffer = m_normalStream.defaultBufferHandle;
        outBindings.indexBuffer = m_indexStream.defaultBufferHandle;
        outBindings.meshRangeBuffer = m_meshRangeState.defaultBufferHandle;
        outBindings.meshRangeSrv = m_meshRangeState.srvHandle;
        return Result::ok();
    }
}
