#pragma once

// === RHI Includes ===
#include <StaticMeshPool.h>
#include <Container/RingBuffer.h>

// === C++ includes ===
#include <string>
#include <unordered_map>
#include <vector>

// === DirectX 12 includes ===
#include "stdafx.h"
#include "DX12BufferManager.h"
#include "DX12ViewManager.h"
#include "DX12GpuCommand.h"

namespace Cue::RHI::DX12
{
    struct StaticMeshRecord final
    {
        Core::ResourceNameId nameId = 0;
        uint32_t meshId = 0;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        uint64_t positionByteOffset = 0;
        uint64_t positionByteSize = 0;
        uint64_t uvByteOffset = 0;
        uint64_t uvByteSize = 0;
        uint64_t normalByteOffset = 0;
        uint64_t normalByteSize = 0;
        uint64_t indexByteOffset = 0;
        uint64_t indexByteSize = 0;
    };

    struct FreeRange final
    {
        uint64_t byteOffset = 0;
        uint64_t byteSize = 0;
    };

    struct StreamState final
    {
        std::string debugName{};
        BufferType bufferType = BufferType::Unknown;
        BufferHandle defaultBufferHandle = {};
        BufferHandle stagingBufferHandle = {};
        std::vector<FreeRange> freeRanges{};
        Core::RingBuffer stagingRing{};
        std::byte* mappedStagingData = nullptr;
        uint64_t capacityInBytes = 0;
        uint64_t stagingCapacityInBytes = 0;
        uint32_t alignment = 1;
    };

    struct MeshRangeState final
    {
        std::string debugName{};
        BufferHandle defaultBufferHandle = {};
        BufferHandle stagingBufferHandle = {};
        ViewHandle srvHandle = {};
        Core::RingBuffer stagingRing{};
        std::byte* mappedStagingData = nullptr;
        uint64_t stagingCapacityInBytes = 0;
        uint32_t capacity = 0;
        std::vector<uint32_t> freeMeshIds{};
    };

    struct UploadAllocation final
    {
        BufferHandle bufferHandle = {};
        std::byte* mappedData = nullptr;
        uint64_t byteOffset = 0;
        uint64_t byteSize = 0;
        Core::RingBuffer::Allocation ringAllocation{};
        bool isTransient = false;

        [[nodiscard]] bool valid() const noexcept
        {
            return bufferHandle.valid() && mappedData != nullptr && byteSize > 0;
        }
    };

    class DX12StaticMeshPool final : public IStaticMeshPool
    {
    public:
        DX12StaticMeshPool(
            const StaticMeshPoolDesc& desc,
            DX12BufferManager& bufferManager,
            DX12ViewManager& viewManager,
            DX12CommandPool& commandPool,
            DX12QueuePool& queuePool);
        ~DX12StaticMeshPool() override;

        Result allocate_mesh(const Core::Native::MeshData& meshData, StaticMeshHandle& outHandle) override;
        Result free_mesh(StaticMeshHandle handle) override;
        Result get_mesh_id(StaticMeshHandle handle, uint32_t& outMeshId) const override;
        Result get_mesh_range(uint32_t meshId, StaticMeshRange& outMeshRange) const override;
        Result get_bindings(StaticMeshPoolBindings& outBindings) const override;
    private:
        Result initialize_streams(const StaticMeshPoolDesc& desc);
        Result initialize_mesh_range_state(const StaticMeshPoolDesc& desc);
        Result create_stream_state(
            std::string_view bufferName,
            BufferType bufferType,
            uint64_t totalBytes,
            uint64_t stagingBytes,
            uint32_t stride,
            uint32_t elementCount,
            uint32_t alignment,
            StreamState& outStreamState);
        Result create_upload_buffer(
            std::string_view bufferName,
            BufferType bufferType,
            uint64_t byteSize,
            BufferHandle& outBufferHandle,
            std::byte*& outMappedData);
        Result allocate_stream_range(
            StreamState& streamState,
            uint64_t byteSize,
            uint32_t alignment,
            uint64_t& outOffset);
        void release_stream_range(
            StreamState& streamState,
            uint64_t byteOffset,
            uint64_t byteSize);
        Result allocate_upload_range(
            StreamState& streamState,
            uint64_t byteSize,
            uint32_t alignment,
            UploadAllocation& outAllocation);
        Result allocate_upload_range(
            MeshRangeState& meshRangeState,
            uint64_t byteSize,
            uint32_t alignment,
            UploadAllocation& outAllocation);
        void release_upload_range(
            StreamState& streamState,
            UploadAllocation& allocation);
        void release_upload_range(
            MeshRangeState& meshRangeState,
            UploadAllocation& allocation);
        void write_upload_bytes(
            const UploadAllocation& allocation,
            const void* sourceData,
            uint64_t byteSize);
        Result copy_upload_regions(const std::vector<BufferCopyRegion>& regions);
        void destroy_stream_state(StreamState& streamState);
        Result allocate_mesh_id(uint32_t& outMeshId);
        void release_mesh_id(uint32_t meshId);
        void write_mesh_range(uint32_t meshId, const StaticMeshRange& meshRange);
        Result upload_mesh_range(uint32_t meshId, const StaticMeshRange& meshRange);
    private:
        DX12BufferManager& m_bufferManager;
        DX12ViewManager& m_viewManager;
        DX12CommandPool& m_commandPool;
        DX12QueuePool& m_queuePool;
        Core::Registry<StaticMeshTag, StaticMeshRecord> m_meshRegistry;
        std::unordered_map<Core::ResourceNameId, StaticMeshHandle> m_nameToHandlesMap;
        std::unordered_map<uint32_t, StaticMeshHandle> m_meshIdToHandlesMap;
        StreamState m_positionStream{};
        StreamState m_uvStream{};
        StreamState m_normalStream{};
        StreamState m_indexStream{};
        MeshRangeState m_meshRangeState{};
        Result m_initResult = Result::ok();
    };
}
