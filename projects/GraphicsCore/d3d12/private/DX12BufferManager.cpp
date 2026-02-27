#include "DX12BufferManager.h"

namespace Cue::GraphicsCore::DX12
{
    DX12BufferManager::DX12BufferManager(RenderDevice& renderDevice)
        : m_renderDevice(renderDevice)
    {
        // 1) DescriptorAllocator を初期化する（仮の容量）
        m_descriptorAllocator = std::make_unique<DescriptorAllocator>(m_renderDevice);
        Result r = m_descriptorAllocator->initialize(
            /*texCap=*/256,
            /*bufCap=*/256,
            /*rtCap=*/32,
            /*dsCap=*/2);
        Assert::cue_assert(r, "DX12BufferManager", "Failed to initialize DescriptorAllocator: {}", r.message);
    }
} // namespace Cue::GraphicsCore::DX12
