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

        BufferHandle create_buffer(const BufferDesc& desc) override
        {
            // 1) 現状MVPでは空レコードを登録してハンドルだけ発行する。
            (void)desc;
            GpuBufferResource resource; // 仮のGpuBufferResourceクラス
            return m_bufferRegistry.create(resource);
        }

        DescriptorAllocator& get_descriptor_allocator() { return *m_descriptorAllocator; }
    private:
        RenderDevice& m_renderDevice; // RenderDeviceへの参照
        Registry<BufferTag, GpuBufferResource> m_bufferRegistry;
        std::unique_ptr<DescriptorAllocator> m_descriptorAllocator = nullptr;
    };

} // namespace Cue::GraphicsCore::DX12
