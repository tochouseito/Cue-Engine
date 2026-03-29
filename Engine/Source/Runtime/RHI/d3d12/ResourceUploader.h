#pragma once

// === RHI includes ===
#include <RHICommon.h>

// === C++ includes ===
#include <vector>

// === DirectX 12 includes ===
#include "DX12BufferManager.h"
#include "DX12GpuCommand.h"

namespace Cue::RHI::DX12
{
    struct BufferUploadRegion final
    {
        BufferHandle srcBufferHandle = {};
        uint32_t srcUploadResourceIndex = 0;
        uint64_t srcByteOffset = 0;
        BufferHandle dstBufferHandle = {};
        uint32_t dstDefaultResourceIndex = 0;
        uint64_t dstByteOffset = 0;
        uint64_t byteSize = 0;
    };

    class ResourceUploader final
    {
    public:
        ResourceUploader(
            DX12BufferManager& a_bufferManager,
            DX12CommandPool& a_commandPool,
            DX12QueuePool& a_queuePool);
        ~ResourceUploader() = default;

        Result upload_buffer_region(const BufferUploadRegion& a_region);
        Result upload_buffer_regions(const std::vector<BufferUploadRegion>& a_regions);
    private:
        Result resolve_upload_resource(
            BufferHandle a_handle,
            uint32_t a_resourceIndex,
            DX12GpuResource** a_outResource);
        Result resolve_default_resource(
            BufferHandle a_handle,
            uint32_t a_resourceIndex,
            DX12GpuResource** a_outResource);
        Result transition_resource(
            ID3D12GraphicsCommandList& a_commandList,
            DX12GpuResource& a_resource,
            D3D12_RESOURCE_STATES a_newState);
    private:
        DX12BufferManager& m_bufferManager;
        DX12CommandPool& m_commandPool;
        DX12QueuePool& m_queuePool;
    };
}
