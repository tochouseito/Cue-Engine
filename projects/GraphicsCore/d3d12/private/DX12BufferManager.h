#pragma once
#include "stdafx.h"
#include "DX12RenderDevice.h"
#include "GpuBuffer.h"
#include "DescriptorAllocator.h"
#include <BufferManager.h>

namespace Cue::GraphicsCore::DX12
{
    class DX12BufferManager final : public IBufferManager
    {
    public:
        DX12BufferManager(DX12RenderDevice& renderDevice)
            : m_renderDevice(renderDevice)
        {
        }
        ~DX12BufferManager() override = default;

        Result create_buffer(const BufferDesc& desc, BufferHandle& outHandle) override;
        Result destroy_buffer(const BufferHandle& handle) override;
        Result get_buffer(ResourceNameId nameId, uint32_t bufferIndex, BufferHandle& outHandle) override;
    private:
        DX12RenderDevice& m_renderDevice; // RenderDeviceへの参照
        Registry<BufferTag, GpuBufferResource> m_bufferRegistry;
        std::unordered_map<ResourceNameId, std::vector<BufferHandle>> m_bufferNameMap;
    };

} // namespace Cue::GraphicsCore::DX12
