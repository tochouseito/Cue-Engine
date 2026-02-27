#pragma once
#include "stdafx.h"
#include "RenderDevice.h"
#include "DescriptorAllocator.h"
#include "DX12GpuCommand.h"

namespace Cue::GraphicsCore::DX12
{
    class SwapChain final
    {
    public:
        SwapChain(RenderDevice& renderDevice, QueuePool& queuePool, DescriptorAllocator& descAllocator)
            : m_renderDevice(renderDevice), m_queuePool(queuePool), m_descriptorAllocator(descAllocator)
        {
        }
        ~SwapChain() = default;

        [[nodiscard]] Result initialize(
            HWND hWnd,
            uint32_t width,
            uint32_t height,
            DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM,
            uint32_t bufferCount = 2);

        ComPtr<ID3D12Resource> get_back_buffer(uint32_t index) const;
        DescriptorAllocator::TableID get_rtv_table_id(uint32_t index) const;
    private:
        RenderDevice& m_renderDevice;
        QueuePool& m_queuePool;
        DescriptorAllocator& m_descriptorAllocator;

        HWND m_hWnd = nullptr;
        ComPtr<IDXGISwapChain4> m_swapChain;
        DXGI_SWAP_CHAIN_DESC1 m_desc{};
        int32_t m_refreshrate = 60;
        std::vector<DescriptorAllocator::TableID> m_rtvTableIDs;
    };
}
