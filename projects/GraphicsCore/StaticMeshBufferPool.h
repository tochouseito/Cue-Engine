#pragma once
#include "BufferManager.h"
#include "GraphicsInterface.h"
#include <Native/EngineNativeStruct.h>

namespace Cue::GraphicsCore
{
    struct StaticMeshBufferPoolDesc final
    {
        uint32_t positionBufferSize = 16u * 1024u * 1024u;
        uint32_t uvBufferSize = 8u * 1024u * 1024u;
        uint32_t normalBufferSize = 12u * 1024u * 1024u;
        uint32_t indexBufferSize = 4u * 1024u * 1024u;
        std::string_view positionBufferName = "StaticMeshPool.Position";
        std::string_view uvBufferName = "StaticMeshPool.Uv";
        std::string_view normalBufferName = "StaticMeshPool.Normal";
        std::string_view indexBufferName = "StaticMeshPool.Index";
    };

    struct VertexStreamSlice final
    {
        BufferHandle buffer{};
        uint64_t byteOffset = 0;
        uint32_t byteSize = 0;
        uint32_t stride = 0;
    };

    struct IndexBufferSlice final
    {
        BufferHandle buffer{};
        uint64_t byteOffset = 0;
        uint32_t byteSize = 0;
        uint32_t indexCount = 0;
        IndexFormat format = IndexFormat::UInt32;
    };

    struct StaticMeshAllocation final
    {
        std::string name{};
        uint32_t vertexCount = 0;
        VertexStreamSlice positionBuffer{};
        VertexStreamSlice uvBuffer{};
        VertexStreamSlice normalBuffer{};
        IndexBufferSlice indexBuffer{};
    };

    struct StaticMeshAllocationTag {};

    using StaticMeshAllocationHandle = Handle<StaticMeshAllocationTag>;

    class StaticMeshBufferPool final
    {
    public:
        StaticMeshBufferPool() = default;
        ~StaticMeshBufferPool() = default;

        Result initialize(
            const StaticMeshBufferPoolDesc& desc,
            IBufferManager& bufferManager,
            ICommandPool& commandPool,
            IQueuePool& queuePool);
        Result shutdown();
        [[nodiscard]] bool is_initialized() const noexcept;

        Result upload_mesh(std::string_view name, const Core::Native::MeshData& meshData, StaticMeshAllocationHandle& outHandle);
        Result upload_model(const Core::Native::ModelData& modelData, std::vector<StaticMeshAllocationHandle>& outHandles);
        Result get_allocation(StaticMeshAllocationHandle handle, StaticMeshAllocation& outAllocation) const;
        Result get_allocation(ResourceNameId nameId, StaticMeshAllocation& outAllocation) const;

    private:
        [[nodiscard]] Result upload_bytes(
            BufferHandle poolBuffer,
            uint64_t dstByteOffset,
            BufferType uploadType,
            const void* data,
            uint32_t byteSize);
        [[nodiscard]] Result create_pool_buffer(
            std::string_view name,
            BufferType type,
            uint32_t byteSize,
            BufferHandle& outHandle);
        [[nodiscard]] static uint64_t align_up(uint64_t value, uint64_t alignment) noexcept;

    private:
        StaticMeshBufferPoolDesc m_desc{};
        IBufferManager* m_bufferManager = nullptr;
        ICommandPool* m_commandPool = nullptr;
        IQueuePool* m_queuePool = nullptr;
        BufferHandle m_positionBufferHandle{};
        BufferHandle m_uvBufferHandle{};
        BufferHandle m_normalBufferHandle{};
        BufferHandle m_indexBufferHandle{};
        uint64_t m_nextPositionByteOffset = 0;
        uint64_t m_nextUvByteOffset = 0;
        uint64_t m_nextNormalByteOffset = 0;
        uint64_t m_nextIndexByteOffset = 0;
        Registry<StaticMeshAllocationTag, StaticMeshAllocation> m_allocationRegistry;
        std::unordered_map<ResourceNameId, StaticMeshAllocationHandle> m_nameMap;
    };
} // namespace Cue::GraphicsCore
