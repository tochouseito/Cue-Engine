#pragma once

// === RHI Include ===
#include <RHICommon.h>

// === DirectX 12 Include ===
#include "stdafx.h"
#include "DX12RenderDevice.h"
#include "DescriptorAllocator.h"
#include "DX12GpuCommand.h"

namespace Cue::RHI::DX12
{
    class SwapChain final
    {
    public:
        SwapChain(DX12RenderDevice& renderDevice, DescriptorAllocator& descriptorAllocator) :
            m_renderDevice(renderDevice),
            m_descriptorAllocator(descriptorAllocator)
        {
        }
        ~SwapChain() = default;

        Result create(
            HWND a_hwnd,
            uint32_t width,
            uint32_t height,
            uint32_t bufferCount,
            DX12GpuCommandQueue& commandQueue,
            DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM);

        Result present(bool vsync);
        uint32_t current_back_buffer_index() const;
        const std::vector<ComPtr<ID3D12Resource>>& back_buffer_resources() const noexcept { return m_backBuffers; }
    private:
        DX12RenderDevice& m_renderDevice;
        DescriptorAllocator& m_descriptorAllocator;
        ComPtr<IDXGISwapChain4> m_swapChain;
        int32_t m_refreshrate = 60; // デフォルトは 60Hz
        std::vector<TableID> m_rtvTableIDs;
        std::vector<ComPtr<ID3D12Resource>> m_backBuffers;
    };
}
