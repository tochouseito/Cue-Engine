#pragma once

// === RHI Include ===
#include <RHICommon.h>

// === DirectX 12 Include ===
#include "stdafx.h"
#include "DX12RenderDevice.h"
#include "DX12GpuCommand.h"
#include "DX12TextureManager.h"
#include "DX12ViewManager.h"
#include "DX12GpuResource.h"

namespace Cue::RHI::DX12
{
    class SwapChain final
    {
    public:
        SwapChain(DX12RenderDevice& renderDevice, DX12TextureManager& textureManager, DX12ViewManager& viewManager) :
            m_renderDevice(renderDevice),
            m_textureManager(textureManager),
            m_viewManager(viewManager)
        {
        }
        ~SwapChain() = default;

        Result create(
            HWND a_hwnd,
            uint32_t width,
            uint32_t height,
            const uint32_t& bufferCount,
            DX12GpuCommandQueue& commandQueue,
            DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM);

        Result present(bool vsync);
        void shutdown();

        uint32_t get_current_back_buffer_index() const noexcept
        {
            return static_cast<uint32_t>(m_swapChain->GetCurrentBackBufferIndex());
        }
        textureHandle get_back_buffer_texture_handle() const noexcept
        {
            return m_backBufferTextureHandle;
        }
        viewHandle get_back_buffer_rtv_view_handle() const noexcept
        {
            return m_rtvViewHandle;
        }
        uint32_t width() const noexcept { return m_width; }
        uint32_t height() const noexcept { return m_height; }
    private:
        DX12RenderDevice& m_renderDevice;
        DX12TextureManager& m_textureManager;
        DX12ViewManager& m_viewManager;
        comPtr<IDXGISwapChain4> m_swapChain;
        int32_t m_refreshrate = 60; // デフォルトは 60Hz
        viewHandle m_rtvViewHandle; // バックバッファの RTV ビュー
        textureHandle m_backBufferTextureHandle;
        uint32_t m_width = 0;
        uint32_t m_height = 0;
    };
}
