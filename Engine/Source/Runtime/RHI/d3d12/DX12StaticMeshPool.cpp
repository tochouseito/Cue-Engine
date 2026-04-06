#include "DX12StaticMeshPool.h"

// === C++ includes ===
#include <algorithm>
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
        // 1) 生成順の逆順で破棄し、staging/default の両方を BufferManager へ戻す。
        if (m_meshRangeState.srvHandle.valid())
        {
            m_viewManager.destroy_view(m_meshRangeState.srvHandle);
            m_meshRangeState.srvHandle = {};
        }
        if (m_meshRangeState.stagingBufferHandle.valid())
        {
            m_bufferManager.destroy_buffer(m_meshRangeState.stagingBufferHandle);
            m_meshRangeState.stagingBufferHandle = {};
        }
        if (m_meshRangeState.defaultBufferHandle.valid())
        {
            m_bufferManager.destroy_buffer(m_meshRangeState.defaultBufferHandle);
            m_meshRangeState.defaultBufferHandle = {};
        }
        m_meshRangeState.debugName.clear();
        m_meshRangeState.stagingRing.clear();
        m_meshRangeState.mappedStagingData = nullptr;
        m_meshRangeState.stagingCapacityInBytes = 0;
        m_meshRangeState.capacity = 0;
        m_meshRangeState.freeMeshIds.clear();
        destroy_stream_state(m_indexStream);
        destroy_stream_state(m_normalStream);
        destroy_stream_state(m_uvStream);
        destroy_stream_state(m_positionStream);
    }

    Result DX12StaticMeshPool::initialize_streams(const StaticMeshPoolDesc& desc)
    {
        // 1) 各ストリームごとの総容量を確定し、永続 default と小さい常設 staging を作る。
        Result result = create_stream_state(
            desc.positionName,
            BufferType::Vertex,
            static_cast<uint64_t>(desc.maxVertexCount) * sizeof(Math::float4),
            desc.positionStagingSize,
            sizeof(Math::float4),
            desc.maxVertexCount,
            alignof(Math::float4),
            m_positionStream);
        if (!result)
        {
            return result;
        }

        result = create_stream_state(
            desc.uvName,
            BufferType::Vertex,
            static_cast<uint64_t>(desc.maxVertexCount) * sizeof(Math::float2),
            desc.uvStagingSize,
            sizeof(Math::float2),
            desc.maxVertexCount,
            alignof(Math::float2),
            m_uvStream);
        if (!result)
        {
            return result;
        }

        result = create_stream_state(
            desc.normalName,
            BufferType::Vertex,
            static_cast<uint64_t>(desc.maxVertexCount) * sizeof(Math::float3),
            desc.normalStagingSize,
            sizeof(Math::float3),
            desc.maxVertexCount,
            alignof(Math::float3),
            m_normalStream);
        if (!result)
        {
            return result;
        }

        result = create_stream_state(
            desc.indexName,
            BufferType::Index,
            static_cast<uint64_t>(desc.maxIndexCount) * sizeof(uint32_t),
            desc.indexStagingSize,
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
        m_meshRangeState.debugName = std::string(desc.meshRangeName);

        BufferDesc defaultDesc{};
        defaultDesc.name = desc.meshRangeName;
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

        const uint64_t totalBytes = static_cast<uint64_t>(defaultDesc.size);
        const uint64_t stagingBytes = (std::min)(
            totalBytes,
            static_cast<uint64_t>(desc.meshRangeStagingCount) * sizeof(StaticMeshRange));
        result = create_upload_buffer(
            m_meshRangeState.debugName + ".Staging",
            BufferType::Structured,
            stagingBytes,
            m_meshRangeState.stagingBufferHandle,
            m_meshRangeState.mappedStagingData);
        if (!result)
        {
            m_bufferManager.destroy_buffer(m_meshRangeState.defaultBufferHandle);
            m_meshRangeState.defaultBufferHandle = {};
            return result;
        }

        ViewDesc meshRangeSrvDesc{};
        meshRangeSrvDesc.name = desc.meshRangeSrvName;
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

        m_meshRangeState.stagingCapacityInBytes = stagingBytes;
        m_meshRangeState.stagingRing.initialize(static_cast<size_t>(stagingBytes));
        m_meshRangeState.capacity = desc.maxMeshCount;
        m_meshRangeState.freeMeshIds.clear();
        m_meshRangeState.freeMeshIds.reserve(desc.maxMeshCount);
        for (uint32_t meshId = desc.maxMeshCount; meshId > 0; --meshId)
        {
            m_meshRangeState.freeMeshIds.push_back(meshId - 1);
        }

        UploadAllocation initializeUpload{};
        result = allocate_upload_range(
            m_meshRangeState,
            totalBytes,
            alignof(StaticMeshRange),
            initializeUpload);
        if (!result)
        {
            return result;
        }

        std::memset(initializeUpload.mappedData + initializeUpload.byteOffset, 0, static_cast<size_t>(totalBytes));

        BufferCopyRegion initializeRegion{};
        initializeRegion.srcBufferHandle = initializeUpload.bufferHandle;
        initializeRegion.srcUploadResourceIndex = 0;
        initializeRegion.srcByteOffset = initializeUpload.byteOffset;
        initializeRegion.dstBufferHandle = m_meshRangeState.defaultBufferHandle;
        initializeRegion.dstDefaultResourceIndex = 0;
        initializeRegion.dstByteOffset = 0;
        initializeRegion.byteSize = totalBytes;
        std::vector<BufferCopyRegion> initializeRegions{ initializeRegion };
        result = copy_upload_regions(initializeRegions);
        release_upload_range(m_meshRangeState, initializeUpload);
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
        uint64_t stagingBytes,
        uint32_t stride,
        uint32_t elementCount,
        uint32_t alignment,
        StreamState& outStreamState)
    {
        // 1) 常駐先は default heap、通常のコピー元は小さい staging upload buffer とする。
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

        outStreamState.debugName = std::string(bufferName);
        outStreamState.bufferType = bufferType;
        const uint64_t clampedStagingBytes = (std::min)(totalBytes, stagingBytes);
        result = create_upload_buffer(
            outStreamState.debugName + ".Staging",
            bufferType,
            clampedStagingBytes,
            outStreamState.stagingBufferHandle,
            outStreamState.mappedStagingData);
        if (!result)
        {
            m_bufferManager.destroy_buffer(outStreamState.defaultBufferHandle);
            outStreamState.defaultBufferHandle = {};
            return result;
        }

        outStreamState.capacityInBytes = totalBytes;
        outStreamState.stagingCapacityInBytes = clampedStagingBytes;
        outStreamState.alignment = alignment;
        outStreamState.freeRanges.clear();
        outStreamState.freeRanges.push_back(FreeRange{ 0, totalBytes });
        outStreamState.stagingRing.initialize(static_cast<size_t>(clampedStagingBytes));
        return Result::ok();
    }

    Result DX12StaticMeshPool::create_upload_buffer(
        std::string_view bufferName,
        BufferType bufferType,
        uint64_t byteSize,
        bufferHandle& outBufferHandle,
        std::byte*& outMappedData)
    {
        outBufferHandle = {};
        outMappedData = nullptr;
        if (byteSize == 0)
        {
            return Result::ok();
        }
        if (byteSize > UINT32_MAX)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Upload buffer size exceeds the supported range.");
        }

        BufferDesc uploadDesc{};
        uploadDesc.name = std::string(bufferName);
        uploadDesc.type = bufferType;
        uploadDesc.defaultHeapCount = 0;
        uploadDesc.uploadHeapCount = 1;
        uploadDesc.initialState = ResourceState::CopySource;
        uploadDesc.stride = 1;
        uploadDesc.elementCount = static_cast<uint32_t>(byteSize);
        uploadDesc.size = static_cast<uint32_t>(byteSize);
        uploadDesc.alignment = 1;

        Result result = m_bufferManager.create_buffer(uploadDesc, outBufferHandle);
        if (!result)
        {
            return result;
        }

        UploadBufferView uploadView{};
        result = m_bufferManager.get_upload_buffer_view(outBufferHandle, uploadView);
        if (!result)
        {
            m_bufferManager.destroy_buffer(outBufferHandle);
            outBufferHandle = {};
            return result;
        }
        if (uploadView.mappedDatas.size() != 1 || uploadView.mappedDatas[0] == nullptr)
        {
            m_bufferManager.destroy_buffer(outBufferHandle);
            outBufferHandle = {};
            return Result::fail(
                Code::InternalError,
                Severity::Error,
                "Static mesh pool upload buffer is not mapped.");
        }

        outMappedData = uploadView.mappedDatas[0];
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
        UploadAllocation& outAllocation)
    {
        outAllocation = {};
        if (streamState.stagingBufferHandle.valid()
            && streamState.mappedStagingData != nullptr
            && streamState.stagingRing.allocate(
                static_cast<size_t>(byteSize),
                static_cast<size_t>(alignment),
                outAllocation.ringAllocation))
        {
            outAllocation.bufferHandle = streamState.stagingBufferHandle;
            outAllocation.mappedData = streamState.mappedStagingData;
            outAllocation.byteOffset = outAllocation.ringAllocation.offset;
            outAllocation.byteSize = byteSize;
            outAllocation.isTransient = false;
            return Result::ok();
        }

        Result result = create_upload_buffer(
            std::string_view{},
            streamState.bufferType,
            byteSize,
            outAllocation.bufferHandle,
            outAllocation.mappedData);
        if (!result)
        {
            return result;
        }

        outAllocation.byteOffset = 0;
        outAllocation.byteSize = byteSize;
        outAllocation.isTransient = true;
        return Result::ok();
    }

    Result DX12StaticMeshPool::allocate_upload_range(
        MeshRangeState& meshRangeState,
        uint64_t byteSize,
        uint32_t alignment,
        UploadAllocation& outAllocation)
    {
        outAllocation = {};
        if (meshRangeState.stagingBufferHandle.valid()
            && meshRangeState.mappedStagingData != nullptr
            && meshRangeState.stagingRing.allocate(
                static_cast<size_t>(byteSize),
                static_cast<size_t>(alignment),
                outAllocation.ringAllocation))
        {
            outAllocation.bufferHandle = meshRangeState.stagingBufferHandle;
            outAllocation.mappedData = meshRangeState.mappedStagingData;
            outAllocation.byteOffset = outAllocation.ringAllocation.offset;
            outAllocation.byteSize = byteSize;
            outAllocation.isTransient = false;
            return Result::ok();
        }

        Result result = create_upload_buffer(
            std::string_view{},
            BufferType::Structured,
            byteSize,
            outAllocation.bufferHandle,
            outAllocation.mappedData);
        if (!result)
        {
            return result;
        }

        outAllocation.byteOffset = 0;
        outAllocation.byteSize = byteSize;
        outAllocation.isTransient = true;
        return Result::ok();
    }

    void DX12StaticMeshPool::release_upload_range(
        StreamState& streamState,
        UploadAllocation& allocation)
    {
        if (!allocation.valid())
        {
            return;
        }

        if (allocation.isTransient)
        {
            Result result = m_bufferManager.destroy_buffer(allocation.bufferHandle);
            CUE_ASSERT_MSG(result, "Failed to destroy transient static mesh upload buffer.");
        }
        else if (allocation.ringAllocation.valid())
        {
            const bool released = streamState.stagingRing.release(allocation.ringAllocation);
            CUE_ASSERT_MSG(released, "Failed to release static mesh staging ring allocation.");
        }

        allocation = {};
    }

    void DX12StaticMeshPool::release_upload_range(
        MeshRangeState& meshRangeState,
        UploadAllocation& allocation)
    {
        if (!allocation.valid())
        {
            return;
        }

        if (allocation.isTransient)
        {
            Result result = m_bufferManager.destroy_buffer(allocation.bufferHandle);
            CUE_ASSERT_MSG(result, "Failed to destroy transient mesh range upload buffer.");
        }
        else if (allocation.ringAllocation.valid())
        {
            const bool released = meshRangeState.stagingRing.release(allocation.ringAllocation);
            CUE_ASSERT_MSG(released, "Failed to release mesh range staging ring allocation.");
        }

        allocation = {};
    }

    void DX12StaticMeshPool::write_upload_bytes(
        const UploadAllocation& allocation,
        const void* sourceData,
        uint64_t byteSize)
    {
        // 1) upload staging へ直接書き込み、データ未指定の属性はゼロで埋める。
        std::byte* dst = allocation.mappedData + allocation.byteOffset;
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

        commandContextLease commandContext{};
        Result result = m_commandPool.get_command_context(CommandListType::Copy, commandContext);
        if (!result)
        {
            return result;
        }

        queueContextLease queueContext{};
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
        // 1) staging/default の順に破棄し、BufferManager の所有実体を明示的に返す。
        if (streamState.stagingBufferHandle.valid())
        {
            m_bufferManager.destroy_buffer(streamState.stagingBufferHandle);
            streamState.stagingBufferHandle = {};
        }
        if (streamState.defaultBufferHandle.valid())
        {
            m_bufferManager.destroy_buffer(streamState.defaultBufferHandle);
            streamState.defaultBufferHandle = {};
        }

        streamState.debugName.clear();
        streamState.bufferType = BufferType::Unknown;
        streamState.freeRanges.clear();
        streamState.stagingRing.clear();
        streamState.mappedStagingData = nullptr;
        streamState.capacityInBytes = 0;
        streamState.stagingCapacityInBytes = 0;
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
        Result result = upload_mesh_range(meshId, meshRange);
        CUE_ASSERT_MSG(result, "Failed to write mesh range.");
    }

    Result DX12StaticMeshPool::upload_mesh_range(uint32_t meshId, const StaticMeshRange& meshRange)
    {
        if (meshId >= m_meshRangeState.capacity)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Mesh range slot is out of bounds.");
        }

        UploadAllocation uploadAllocation{};
        Result result = allocate_upload_range(
            m_meshRangeState,
            sizeof(StaticMeshRange),
            alignof(StaticMeshRange),
            uploadAllocation);
        if (!result)
        {
            return result;
        }

        write_upload_bytes(uploadAllocation, &meshRange, sizeof(StaticMeshRange));

        BufferCopyRegion region{};
        region.srcBufferHandle = uploadAllocation.bufferHandle;
        region.srcUploadResourceIndex = 0;
        region.srcByteOffset = uploadAllocation.byteOffset;
        region.dstBufferHandle = m_meshRangeState.defaultBufferHandle;
        region.dstDefaultResourceIndex = 0;
        region.dstByteOffset = static_cast<uint64_t>(meshId) * sizeof(StaticMeshRange);
        region.byteSize = sizeof(StaticMeshRange);

        std::vector<BufferCopyRegion> regions{ region };
        result = copy_upload_regions(regions);
        release_upload_range(m_meshRangeState, uploadAllocation);
        return result;
    }

    Result DX12StaticMeshPool::allocate_mesh(const Core::Native::MeshData& meshData, staticMeshHandle& outHandle)
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

        // 3) 常設 staging に乗る分は ring を使い、乗らない分だけ一時 upload buffer へ逃がす。
        UploadAllocation positionUpload{};
        UploadAllocation uvUpload{};
        UploadAllocation normalUpload{};
        UploadAllocation indexUpload{};

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

        UploadAllocation meshRangeUpload{};
        StaticMeshRange meshRange{};
        meshRange.indexCount = record.indexCount;
        meshRange.startIndex = static_cast<uint32_t>(record.indexByteOffset / sizeof(uint32_t));
        meshRange.baseVertex = static_cast<int32_t>(record.positionByteOffset / sizeof(Math::float4));
        result = allocate_upload_range(
            m_meshRangeState,
            sizeof(StaticMeshRange),
            alignof(StaticMeshRange),
            meshRangeUpload);
        if (!result)
        {
            release_upload_range(m_indexStream, indexUpload);
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

        write_upload_bytes(positionUpload, meshData.positions.data(), record.positionByteSize);
        write_upload_bytes(
            uvUpload,
            meshData.uvs.empty() ? nullptr : meshData.uvs.data(),
            record.uvByteSize);
        write_upload_bytes(
            normalUpload,
            meshData.normals.empty() ? nullptr : meshData.normals.data(),
            record.normalByteSize);
        write_upload_bytes(indexUpload, meshData.indices.data(), record.indexByteSize);
        write_upload_bytes(meshRangeUpload, &meshRange, sizeof(StaticMeshRange));

        std::vector<BufferCopyRegion> uploadRegions
        {
            BufferCopyRegion
            {
                .srcBufferHandle = positionUpload.bufferHandle,
                .srcUploadResourceIndex = 0,
                .srcByteOffset = positionUpload.byteOffset,
                .dstBufferHandle = m_positionStream.defaultBufferHandle,
                .dstDefaultResourceIndex = 0,
                .dstByteOffset = record.positionByteOffset,
                .byteSize = record.positionByteSize
            },
            BufferCopyRegion
            {
                .srcBufferHandle = uvUpload.bufferHandle,
                .srcUploadResourceIndex = 0,
                .srcByteOffset = uvUpload.byteOffset,
                .dstBufferHandle = m_uvStream.defaultBufferHandle,
                .dstDefaultResourceIndex = 0,
                .dstByteOffset = record.uvByteOffset,
                .byteSize = record.uvByteSize
            },
            BufferCopyRegion
            {
                .srcBufferHandle = normalUpload.bufferHandle,
                .srcUploadResourceIndex = 0,
                .srcByteOffset = normalUpload.byteOffset,
                .dstBufferHandle = m_normalStream.defaultBufferHandle,
                .dstDefaultResourceIndex = 0,
                .dstByteOffset = record.normalByteOffset,
                .byteSize = record.normalByteSize
            },
            BufferCopyRegion
            {
                .srcBufferHandle = indexUpload.bufferHandle,
                .srcUploadResourceIndex = 0,
                .srcByteOffset = indexUpload.byteOffset,
                .dstBufferHandle = m_indexStream.defaultBufferHandle,
                .dstDefaultResourceIndex = 0,
                .dstByteOffset = record.indexByteOffset,
                .byteSize = record.indexByteSize
            },
            BufferCopyRegion
            {
                .srcBufferHandle = meshRangeUpload.bufferHandle,
                .srcUploadResourceIndex = 0,
                .srcByteOffset = meshRangeUpload.byteOffset,
                .dstBufferHandle = m_meshRangeState.defaultBufferHandle,
                .dstDefaultResourceIndex = 0,
                .dstByteOffset = static_cast<uint64_t>(record.meshId) * sizeof(StaticMeshRange),
                .byteSize = sizeof(StaticMeshRange)
            }
        };

        result = copy_upload_regions(uploadRegions);

        release_upload_range(m_meshRangeState, meshRangeUpload);
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

        staticMeshHandle handle = m_meshRegistry.create(record);
        if (record.nameId != 0)
        {
            m_nameToHandlesMap[record.nameId] = handle;
        }

        outHandle = handle;
        return Result::ok();
    }

    Result DX12StaticMeshPool::free_mesh(staticMeshHandle handle)
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

    Result DX12StaticMeshPool::get_mesh_id(staticMeshHandle handle, uint32_t& outMeshId) const
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
