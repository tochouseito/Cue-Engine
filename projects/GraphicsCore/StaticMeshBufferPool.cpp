#include "StaticMeshBufferPool.h"

namespace Cue::GraphicsCore
{
    namespace
    {
        constexpr uint64_t k_positionAlignment = 16u;
        constexpr uint64_t k_uvAlignment = 8u;
        constexpr uint64_t k_normalAlignment = 16u;
        constexpr uint64_t k_indexAlignment = 4u;

        [[nodiscard]] Result validate_mesh_data(const Core::Native::MeshData& meshData)
        {
            // 1) 空メッシュを早期に弾き、巨大 pool へ無意味な領域を予約しない。
            if (meshData.positions.empty())
            {
                return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Warning, 0, "Mesh position stream is empty.");
            }
            if (meshData.uvs.size() != meshData.positions.size())
            {
                return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Warning, 0, "Mesh uv stream count does not match position count.");
            }
            if (meshData.normals.size() != meshData.positions.size())
            {
                return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Warning, 0, "Mesh normal stream count does not match position count.");
            }
            if (meshData.indices.empty())
            {
                return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Warning, 0, "Mesh index data is empty.");
            }

            return Result::ok();
        }
    }

    Result StaticMeshBufferPool::initialize(
        const StaticMeshBufferPoolDesc& desc,
        IBufferManager& bufferManager,
        ICommandPool& commandPool,
        IQueuePool& queuePool)
    {
        // 1) pool 容量が 0 のまま初期化されると後段の upload で必ず失敗するため、入口で止める。
        if (desc.positionBufferSize == 0 || desc.uvBufferSize == 0 || desc.normalBufferSize == 0 || desc.indexBufferSize == 0)
        {
            return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Error, 0, "Static mesh pool size must be greater than zero.");
        }
        if (is_initialized())
        {
            return Result::fail(Facility::Graphics, Code::InvalidState, Severity::Error, 0, "Static mesh pool is already initialized.");
        }

        // 2) backend 所有 manager/pool への参照を保持し、以後の upload を GraphicsCore 抽象経由に揃える。
        m_desc = desc;
        m_bufferManager = &bufferManager;
        m_commandPool = &commandPool;
        m_queuePool = &queuePool;

        const Result createPositionBufferResult = create_pool_buffer(m_desc.positionBufferName, BufferType::Vertex, m_desc.positionBufferSize, m_positionBufferHandle);
        if (!createPositionBufferResult)
        {
            shutdown();
            return createPositionBufferResult;
        }

        const Result createUvBufferResult = create_pool_buffer(m_desc.uvBufferName, BufferType::Vertex, m_desc.uvBufferSize, m_uvBufferHandle);
        if (!createUvBufferResult)
        {
            shutdown();
            return createUvBufferResult;
        }

        const Result createNormalBufferResult = create_pool_buffer(m_desc.normalBufferName, BufferType::Vertex, m_desc.normalBufferSize, m_normalBufferHandle);
        if (!createNormalBufferResult)
        {
            shutdown();
            return createNormalBufferResult;
        }

        const Result createIndexBufferResult = create_pool_buffer(m_desc.indexBufferName, BufferType::Index, m_desc.indexBufferSize, m_indexBufferHandle);
        if (!createIndexBufferResult)
        {
            shutdown();
            return createIndexBufferResult;
        }

        return Result::ok();
    }

    Result StaticMeshBufferPool::shutdown()
    {
        // 1) allocation 情報を先に捨て、以後の handle 参照を止めてから GPU バッファを破棄する。
        m_allocationRegistry = {};
        m_nameMap.clear();
        m_nextPositionByteOffset = 0;
        m_nextUvByteOffset = 0;
        m_nextNormalByteOffset = 0;
        m_nextIndexByteOffset = 0;

        Result firstError = Result::ok();
        if (m_bufferManager != nullptr && m_positionBufferHandle.valid())
        {
            const Result destroyResult = m_bufferManager->destroy_buffer(m_positionBufferHandle);
            if (!destroyResult && firstError)
            {
                firstError = destroyResult;
            }
        }
        if (m_bufferManager != nullptr && m_uvBufferHandle.valid())
        {
            const Result destroyResult = m_bufferManager->destroy_buffer(m_uvBufferHandle);
            if (!destroyResult && firstError)
            {
                firstError = destroyResult;
            }
        }
        if (m_bufferManager != nullptr && m_normalBufferHandle.valid())
        {
            const Result destroyResult = m_bufferManager->destroy_buffer(m_normalBufferHandle);
            if (!destroyResult && firstError)
            {
                firstError = destroyResult;
            }
        }
        if (m_bufferManager != nullptr && m_indexBufferHandle.valid())
        {
            const Result destroyResult = m_bufferManager->destroy_buffer(m_indexBufferHandle);
            if (!destroyResult && firstError)
            {
                firstError = destroyResult;
            }
        }

        m_positionBufferHandle = {};
        m_uvBufferHandle = {};
        m_normalBufferHandle = {};
        m_indexBufferHandle = {};
        m_bufferManager = nullptr;
        m_commandPool = nullptr;
        m_queuePool = nullptr;
        return firstError;
    }

    bool StaticMeshBufferPool::is_initialized() const noexcept
    {
        return m_bufferManager != nullptr
            && m_commandPool != nullptr
            && m_queuePool != nullptr
            && m_positionBufferHandle.valid()
            && m_uvBufferHandle.valid()
            && m_normalBufferHandle.valid()
            && m_indexBufferHandle.valid();
    }

    Result StaticMeshBufferPool::upload_mesh(std::string_view name, const Core::Native::MeshData& meshData, StaticMeshAllocationHandle& outHandle)
    {
        // 1) pool 未初期化や空メッシュを入口で止め、GPU バッファ断片化の原因を残さない。
        if (!is_initialized())
        {
            return Result::fail(Facility::Graphics, Code::InvalidState, Severity::Error, 0, "Static mesh pool is not initialized.");
        }
        const Result validateResult = validate_mesh_data(meshData);
        if (!validateResult)
        {
            return validateResult;
        }

        const uint32_t vertexCount = meshData.vertex_count();
        const uint32_t positionByteSize = static_cast<uint32_t>(meshData.positions.size() * sizeof(Math::float4));
        const uint32_t uvByteSize = static_cast<uint32_t>(meshData.uvs.size() * sizeof(Math::float2));
        const uint32_t normalByteSize = static_cast<uint32_t>(meshData.normals.size() * sizeof(Math::float3));
        const uint32_t indexByteSize = static_cast<uint32_t>(meshData.indices.size() * sizeof(uint32_t));
        const uint64_t positionByteOffset = align_up(m_nextPositionByteOffset, k_positionAlignment);
        const uint64_t uvByteOffset = align_up(m_nextUvByteOffset, k_uvAlignment);
        const uint64_t normalByteOffset = align_up(m_nextNormalByteOffset, k_normalAlignment);
        const uint64_t indexByteOffset = align_up(m_nextIndexByteOffset, k_indexAlignment);

        // 2) まだ再構築を持たないため、巨大 pool からの bump allocation で容量超過を即検出する。
        if ((positionByteOffset + positionByteSize) > m_desc.positionBufferSize)
        {
            return Result::fail(Facility::Graphics, Code::OutOfMemory, Severity::Warning, 0, "Static mesh position pool is out of capacity.");
        }
        if ((uvByteOffset + uvByteSize) > m_desc.uvBufferSize)
        {
            return Result::fail(Facility::Graphics, Code::OutOfMemory, Severity::Warning, 0, "Static mesh uv pool is out of capacity.");
        }
        if ((normalByteOffset + normalByteSize) > m_desc.normalBufferSize)
        {
            return Result::fail(Facility::Graphics, Code::OutOfMemory, Severity::Warning, 0, "Static mesh normal pool is out of capacity.");
        }
        if ((indexByteOffset + indexByteSize) > m_desc.indexBufferSize)
        {
            return Result::fail(Facility::Graphics, Code::OutOfMemory, Severity::Warning, 0, "Static mesh index pool is out of capacity.");
        }

        const ResourceNameId nameId = Core::fnv1a64(name);
        if (!name.empty() && m_nameMap.contains(nameId))
        {
            return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Warning, 0, "Static mesh name already exists in pool.");
        }

        const Result uploadPositionResult = upload_bytes(
            m_positionBufferHandle,
            positionByteOffset,
            BufferType::Vertex,
            meshData.positions.data(),
            positionByteSize);
        if (!uploadPositionResult)
        {
            return uploadPositionResult;
        }

        const Result uploadUvResult = upload_bytes(
            m_uvBufferHandle,
            uvByteOffset,
            BufferType::Vertex,
            meshData.uvs.data(),
            uvByteSize);
        if (!uploadUvResult)
        {
            return uploadUvResult;
        }

        const Result uploadNormalResult = upload_bytes(
            m_normalBufferHandle,
            normalByteOffset,
            BufferType::Vertex,
            meshData.normals.data(),
            normalByteSize);
        if (!uploadNormalResult)
        {
            return uploadNormalResult;
        }

        const Result uploadIndexResult = upload_bytes(
            m_indexBufferHandle,
            indexByteOffset,
            BufferType::Index,
            meshData.indices.data(),
            indexByteSize);
        if (!uploadIndexResult)
        {
            return uploadIndexResult;
        }

        // 3) draw 準備はまだ持たないので、後段が bind できる最小 slice 情報だけを registry に残す。
        StaticMeshAllocation allocation{};
        allocation.name = std::string(name);
        allocation.vertexCount = vertexCount;
        allocation.positionBuffer.buffer = m_positionBufferHandle;
        allocation.positionBuffer.byteOffset = positionByteOffset;
        allocation.positionBuffer.byteSize = positionByteSize;
        allocation.positionBuffer.stride = sizeof(Math::float4);
        allocation.uvBuffer.buffer = m_uvBufferHandle;
        allocation.uvBuffer.byteOffset = uvByteOffset;
        allocation.uvBuffer.byteSize = uvByteSize;
        allocation.uvBuffer.stride = sizeof(Math::float2);
        allocation.normalBuffer.buffer = m_normalBufferHandle;
        allocation.normalBuffer.byteOffset = normalByteOffset;
        allocation.normalBuffer.byteSize = normalByteSize;
        allocation.normalBuffer.stride = sizeof(Math::float3);
        allocation.indexBuffer.buffer = m_indexBufferHandle;
        allocation.indexBuffer.byteOffset = indexByteOffset;
        allocation.indexBuffer.byteSize = indexByteSize;
        allocation.indexBuffer.indexCount = static_cast<uint32_t>(meshData.indices.size());
        allocation.indexBuffer.format = IndexFormat::UInt32;

        outHandle = m_allocationRegistry.create(allocation);
        if (!name.empty())
        {
            m_nameMap.emplace(nameId, outHandle);
        }

        m_nextPositionByteOffset = positionByteOffset + positionByteSize;
        m_nextUvByteOffset = uvByteOffset + uvByteSize;
        m_nextNormalByteOffset = normalByteOffset + normalByteSize;
        m_nextIndexByteOffset = indexByteOffset + indexByteSize;
        return Result::ok();
    }

    Result StaticMeshBufferPool::upload_model(const Core::Native::ModelData& modelData, std::vector<StaticMeshAllocationHandle>& outHandles)
    {
        // 1) model 単位 upload はメッシュ配列順を保持し、呼び出し側が sub-mesh 対応表をそのまま使えるようにする。
        outHandles.clear();
        outHandles.reserve(modelData.meshes.size());

        for (const Core::Native::MeshData& meshData : modelData.meshes)
        {
            StaticMeshAllocationHandle handle{};
            const Result uploadResult = upload_mesh(meshData.name, meshData, handle);
            if (!uploadResult)
            {
                return uploadResult;
            }
            outHandles.push_back(handle);
        }

        return Result::ok();
    }

    Result StaticMeshBufferPool::get_allocation(StaticMeshAllocationHandle handle, StaticMeshAllocation& outAllocation) const
    {
        // 1) pool 内 handle 以外を明確に弾き、後段の bind で無効オフセットを作らない。
        if (!m_allocationRegistry.try_get(handle, outAllocation))
        {
            return Result::fail(Facility::Graphics, Code::NotFound, Severity::Warning, 0, "Static mesh allocation handle is not alive.");
        }
        return Result::ok();
    }

    Result StaticMeshBufferPool::get_allocation(ResourceNameId nameId, StaticMeshAllocation& outAllocation) const
    {
        // 1) 名前検索は registry handle に集約し、allocation 実体の複製管理を避ける。
        const auto it = m_nameMap.find(nameId);
        if (it == m_nameMap.end())
        {
            return Result::fail(Facility::Graphics, Code::NotFound, Severity::Warning, 0, "Static mesh allocation name was not found.");
        }

        return get_allocation(it->second, outAllocation);
    }

    Result StaticMeshBufferPool::upload_bytes(
        BufferHandle poolBuffer,
        uint64_t dstByteOffset,
        BufferType uploadType,
        const void* data,
        uint32_t byteSize)
    {
        // 1) CPU データは UploadBuffer へ一度だけ書き、DefaultBuffer への転送経路を backend 共通 API に限定する。
        if (data == nullptr || byteSize == 0)
        {
            return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Error, 0, "Upload source data is invalid.");
        }

        BufferDesc uploadDesc{};
        uploadDesc.name = uploadType == BufferType::Vertex ? "StaticMeshPool.Upload.Vertex" : "StaticMeshPool.Upload.Index";
        uploadDesc.type = uploadType;
        uploadDesc.heapType = ResourceHeapType::Upload;
        uploadDesc.initialState = ResourceState::CopySource;
        uploadDesc.size = byteSize;

        BufferHandle uploadHandle{};
        const Result createUploadResult = m_bufferManager->create_buffer(uploadDesc, uploadHandle);
        if (!createUploadResult)
        {
            return createUploadResult;
        }

        const Result writeResult = m_bufferManager->write_buffer(uploadHandle, 0, data, byteSize);
        if (!writeResult)
        {
            m_bufferManager->destroy_buffer(uploadHandle);
            return writeResult;
        }

        CommandContextLease copyContext{};
        Result result = m_commandPool->acquire_context(CommandListType::Copy, copyContext);
        if (!result)
        {
            m_bufferManager->destroy_buffer(uploadHandle);
            return result;
        }

        QueueContextLease copyQueue{};
        result = m_queuePool->acquire_queue(CommandListType::Copy, copyQueue);
        if (!result)
        {
            m_bufferManager->destroy_buffer(uploadHandle);
            return result;
        }

        result = copyContext->reset();
        if (!result)
        {
            m_bufferManager->destroy_buffer(uploadHandle);
            return result;
        }
        result = copyContext->setup();
        if (!result)
        {
            m_bufferManager->destroy_buffer(uploadHandle);
            return result;
        }

        ResourceBarrierDesc barrierToCopyDest{};
        barrierToCopyDest.kind = ResourceKind::Buffer;
        barrierToCopyDest.index = poolBuffer.index;
        barrierToCopyDest.generation = poolBuffer.generation;
        barrierToCopyDest.before = ResourceState::Common;
        barrierToCopyDest.after = ResourceState::CopyDest;
        result = copyContext->resource_barrier(barrierToCopyDest);
        if (!result)
        {
            m_bufferManager->destroy_buffer(uploadHandle);
            return result;
        }

        result = copyContext->copy_buffer_region(poolBuffer, dstByteOffset, uploadHandle, 0, byteSize);
        if (!result)
        {
            m_bufferManager->destroy_buffer(uploadHandle);
            return result;
        }

        ResourceBarrierDesc barrierToCommon = barrierToCopyDest;
        barrierToCommon.before = ResourceState::CopyDest;
        barrierToCommon.after = ResourceState::Common;
        result = copyContext->resource_barrier(barrierToCommon);
        if (!result)
        {
            m_bufferManager->destroy_buffer(uploadHandle);
            return result;
        }

        result = copyContext->close();
        if (!result)
        {
            m_bufferManager->destroy_buffer(uploadHandle);
            return result;
        }

        result = copyQueue->submit(*copyContext);
        if (!result)
        {
            m_bufferManager->destroy_buffer(uploadHandle);
            return result;
        }

        QueueSyncPoint copyCompletionPoint{};
        result = copyQueue->signal(copyCompletionPoint);
        if (!result)
        {
            return result;
        }

        result = copyQueue->wait_for_point(copyCompletionPoint);
        if (!result)
        {
            return result;
        }

        return m_bufferManager->destroy_buffer(uploadHandle);
    }

    Result StaticMeshBufferPool::create_pool_buffer(
        std::string_view name,
        BufferType type,
        uint32_t byteSize,
        BufferHandle& outHandle)
    {
        // 1) 巨大 pool は Default heap に固定し、GPU 常駐前提の静的メッシュ用途をぶらさない。
        BufferDesc desc{};
        desc.name = name;
        desc.type = type;
        desc.heapType = ResourceHeapType::Default;
        desc.initialState = ResourceState::Common;
        desc.size = byteSize;
        return m_bufferManager->create_buffer(desc, outHandle);
    }

    uint64_t StaticMeshBufferPool::align_up(uint64_t value, uint64_t alignment) noexcept
    {
        if (alignment == 0)
        {
            return value;
        }

        const uint64_t mask = alignment - 1u;
        return (value + mask) & ~mask;
    }
} // namespace Cue::GraphicsCore
