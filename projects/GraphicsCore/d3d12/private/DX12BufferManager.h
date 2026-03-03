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
        DX12BufferManager(DX12RenderDevice& renderDevice);
        ~DX12BufferManager() override = default;

        Result create_buffer(const BufferDesc& desc, BufferHandle& outHandle) override;
        Result destroy_buffer(const BufferHandle& handle) override;
        Result get_buffer(ResourceNameId nameId, uint32_t bufferIndex, BufferHandle& outHandle) override;
    private:
        Registry<BufferTag, GpuBufferResource> m_bufferRegistry;
        std::unordered_map<ResourceNameId, std::vector<BufferHandle>> m_bufferNameMap;
        std::unique_ptr<DescriptorAllocator> m_descriptorAllocator = nullptr;
    };

} // namespace Cue::GraphicsCore::DX12
