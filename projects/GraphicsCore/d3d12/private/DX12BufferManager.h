#pragma once
#include "stdafx.h"
#include "RenderDevice.h"
#include "GpuBuffer.h"
#include "DescriptorAllocator.h"
#include <BufferManager.h>

namespace Cue::GraphicsCore::DX12
{
    class DX12BufferManager final : public BufferManager
    {
    public:
        DX12BufferManager(RenderDevice& renderDevice);
        ~DX12BufferManager() override = default;

        Result create_buffer(const BufferDesc& desc, BufferHandle& outHandle) override;

        DescriptorAllocator& get_descriptor_allocator() { return *m_descriptorAllocator; }
    private:
        RenderDevice& m_renderDevice; // RenderDeviceへの参照
        Registry<BufferTag, GpuBufferResource> m_bufferRegistry;
        std::unique_ptr<DescriptorAllocator> m_descriptorAllocator = nullptr;
    };

} // namespace Cue::GraphicsCore::DX12
