#include "SwapChain.h"

namespace Cue::RHI::DX12
{
    Result SwapChain::create(HWND a_hwnd, uint32_t width, uint32_t height, const uint32_t& bufferCount, DX12GpuCommandQueue& commandQueue, DXGI_FORMAT format)
    {
        m_width = width;
        m_height = height;
        // SwapChainの設定
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = width; // クライアント幅設定
        desc.Height = height; // クライアント高さ設定
        desc.Format = format; // バックバッファ形式設定
        desc.SampleDesc.Count = 1; // 単一サンプル設定
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 描画用途設定
        desc.BufferCount = bufferCount; // バッファ数設定
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // flip discard 設定
        desc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING; // tearing 許可

        // SwapChain 作成
        comPtr<IDXGISwapChain1> swapChain1;
        HRESULT hr = m_renderDevice.get_dxgi_factory()->CreateSwapChainForHwnd(
            commandQueue.command_queue(),
            a_hwnd,
            &desc,
            nullptr, nullptr,
            swapChain1.GetAddressOf());

        CUE_ASSERTF(SUCCEEDED(hr), "Failed to create SwapChain. HRESULT: {:#X}", static_cast<uint32_t>(hr));

        set_dxgi_name(swapChain1.Get(), L"Main SwapChain");

        // IDXGISwapChain4 へキャスト
        swapChain1->QueryInterface(IID_PPV_ARGS(&m_swapChain));

        // リフレッシュレート取得
        // ウィンドウ対応モニター取得
        HMONITOR hMonitor = MonitorFromWindow(a_hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFOEX mi{};
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfo(hMonitor, &mi))
        {
            CUE_ASSERT_MSG(false, "Failed to get monitor info for refresh rate.");
        }
        // 対応ディスプレイ設定取得
        DEVMODE dm{};
        dm.dmSize = sizeof(dm);
        if (!EnumDisplaySettings(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm))
        {
            CUE_ASSERT_MSG(false, "Failed to get display settings for refresh rate.");
        }
        m_refreshrate = static_cast<uint32_t>(dm.dmDisplayFrequency);

        // vsync 併用向けレイテンシ設定
        // m_swapChain->SetMaximumFrameLatency(bufferCount);

        // os 標準 full screen 遷移無効化
        m_renderDevice.get_dxgi_factory()->MakeWindowAssociation(
            a_hwnd,
            DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER);

        std::vector<DX12GpuResource> m_backBuffers;
        for (uint32_t i = 0; i < bufferCount; ++i)
        {
            comPtr<ID3D12Resource> pResource;
            hr = m_swapChain->GetBuffer(i, IID_PPV_ARGS(&pResource));
            if (FAILED(hr))
            {
                CUE_ASSERTF(false, "Failed to get SwapChain back buffer. HRESULT: {:#X}", static_cast<uint32_t>(hr));
            }
            DX12GpuResource backBuffer(pResource, D3D12_RESOURCE_STATE_PRESENT);
            m_backBuffers.push_back(std::move(backBuffer));
        }

        // バックバッファリソースをTextureManagerに登録
        DX12TextureRecord record{};
        record.desc.name = "BackBuffer";
        record.desc.width = width;
        record.desc.height = height;
        record.desc.format = ColorFormat::R8G8B8A8_UNORM;
        record.desc.bufferCount = bufferCount;
        record.defaultResources = std::move(m_backBuffers);
        m_textureManager.register_external_texture(record, m_backBufferTextureHandle);

        // バックバッファの RTV ビューを ViewManager に登録
        ViewDesc viewDesc{};
        viewDesc.name = "BackBufferRTV";
        viewDesc.type = ViewType::RenderTarget;
        viewDesc.textureHandle = m_backBufferTextureHandle;
        viewDesc.bufferKind = BufferKind::Texture;
        viewDesc.colorFormat = convert_color_format(format);
        m_viewManager.create_view(viewDesc, m_rtvViewHandle);

        return Result::ok();
    }
    Result SwapChain::present(bool vsync)
    {
        HRESULT hr = S_OK;
        // vsync 設定で present 引数切替
        if (vsync)
        {
            hr = m_swapChain->Present(1, 0);
        }
        else
        {
            hr = m_swapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
        }
        if (FAILED(hr))
        {
            return Result::fail(
                PAL::Win::convert_hresult_code(hr), Severity::Error,
                "Failed to present SwapChain.");
        }
        return Result::ok();
    }
    void SwapChain::shutdown()
    {
        // バックバッファの RTV ビューを ViewManager から破棄
        m_viewManager.destroy_view(m_rtvViewHandle);
        m_rtvViewHandle = {};

        // バックバッファリソースを TextureManager から破棄
        m_textureManager.destroy_texture(m_backBufferTextureHandle);
        m_backBufferTextureHandle = {};

        // SwapChain を破棄
        m_swapChain.Reset();
    }
}
