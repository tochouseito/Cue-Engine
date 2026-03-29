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
#include "ResourceUploader.h"

namespace Cue::RHI::DX12
{
    struct StaticMeshRecord final
    {
        Core::ResourceNameId nameId = 0;
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
        BufferHandle defaultBufferHandle = {};
        BufferHandle uploadBufferHandle = {};
        std::vector<FreeRange> freeRanges{};
        Core::RingBuffer uploadRing{};
        std::byte* mappedUploadData = nullptr;
        uint64_t capacityInBytes = 0;
        uint32_t alignment = 1;
    };

    class DX12StaticMeshPool final : public IStaticMeshPool
    {
    public:
        DX12StaticMeshPool(
            const StaticMeshPoolDesc& desc,
            DX12BufferManager& bufferManager,
            ResourceUploader& resourceUploader);
        ~DX12StaticMeshPool() override;

        Result allocate_mesh(const Core::Native::MeshData& meshData, StaticMeshHandle& outHandle) override;
        Result free_mesh(StaticMeshHandle handle) override;
    private:
        Result initialize_streams(const StaticMeshPoolDesc& desc);
        Result create_stream_state(
            std::string_view bufferName,
            BufferType bufferType,
            uint64_t totalBytes,
            uint32_t stride,
            uint32_t elementCount,
            uint32_t alignment,
            StreamState& outStreamState);
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
            Core::RingBuffer::Allocation& outAllocation);
        void release_upload_range(
            StreamState& streamState,
            const Core::RingBuffer::Allocation& allocation);
        void write_upload_bytes(
            StreamState& streamState,
            const Core::RingBuffer::Allocation& allocation,
            const void* sourceData,
            uint64_t byteSize);
        void destroy_stream_state(StreamState& streamState);
    private:
        DX12BufferManager& m_bufferManager;
        ResourceUploader& m_resourceUploader;
        Core::Registry<StaticMeshTag, StaticMeshRecord> m_meshRegistry;
        std::unordered_map<Core::ResourceNameId, StaticMeshHandle> m_nameToHandlesMap;
        StreamState m_positionStream{};
        StreamState m_uvStream{};
        StreamState m_normalStream{};
        StreamState m_indexStream{};
        Result m_initResult = Result::ok();
    };
}
