#include "SwapChain.h"

namespace Cue::GraphicsCore::DX12
{
    Result SwapChain::create(HWND hWnd, uint32_t width, uint32_t height, uint32_t bufferCount, DX12QueueContext& queue, DXGI_FORMAT format)
    {
        m_hWnd = hWnd;
        m_width = width;
        m_height = height;
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = width; // クライアント幅設定
        desc.Height = height; // クライアント高さ設定
        desc.Format = format; // バックバッファ形式設定
        desc.SampleDesc.Count = 1; // 単一サンプル設定
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 描画用途設定
        desc.BufferCount = bufferCount; // バッファ数設定
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // flip discard 設定
        desc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING; // tearing 許可

        ComPtr<IDXGISwapChain1> swapChain1;


        HRESULT hr = m_renderDevice.get_dxgi_factory()->CreateSwapChainForHwnd(
            queue.get_command_queue(),
            m_hWnd,
            &desc,
            nullptr, nullptr,
            reinterpret_cast<IDXGISwapChain1**>(m_swapChain.GetAddressOf()));

        Assert::cue_assert(SUCCEEDED(hr), "Failed to create SwapChain. HRESULT: {:#X}", static_cast<uint32_t>(hr));

        SetDXGIName(m_swapChain.Get(), L"Main SwapChain");

        // リフレッシュレート取得
        // ウィンドウ対応モニター取得
        HMONITOR hMonitor = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFOEX mi{};
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfo(hMonitor, &mi))
        {
            Assert::cue_assert(false, "Failed to get monitor info for refresh rate.");
        }
        // 対応ディスプレイ設定取得
        DEVMODE dm{};
        dm.dmSize = sizeof(dm);
        if (!EnumDisplaySettings(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm))
        {
            Assert::cue_assert(false, "Failed to get display settings for refresh rate.");
        }
        m_refreshrate = static_cast<uint32_t>(dm.dmDisplayFrequency);

        // vsync 併用向けレイテンシ設定
        m_swapChain->SetMaximumFrameLatency(bufferCount);

        // os 標準 full screen 遷移無効化
        m_renderDevice.get_dxgi_factory()->MakeWindowAssociation(
            m_hWnd,
            DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER);

        // rtv 作成
        m_rtvTableIDs.resize(bufferCount);
        m_backBuffers.resize(bufferCount);
        for (uint32_t i = 0; i < bufferCount; ++i)
        {
            ComPtr<ID3D12Resource> pResource;
            hr = m_swapChain->GetBuffer(i, IID_PPV_ARGS(&pResource));
            if (FAILED(hr))
            {
                Assert::cue_assert(false, "Failed to get SwapChain back buffer. HRESULT: {:#X}", static_cast<uint32_t>(hr));
            }
            m_backBuffers[i].init(std::move(pResource), D3D12_RESOURCE_STATE_PRESENT, L"SwapChain BackBuffer");
            DescriptorAllocator::TableID rtvTableID = m_descriptorAllocator.allocate(DescriptorAllocator::TableKind::RenderTargets);
            m_descriptorAllocator.create_rtv(rtvTableID, pResource.Get(), format);
            m_rtvTableIDs[i] = rtvTableID;
        }

        return Result::ok();
    }
    Result SwapChain::present(bool vsync)
    {
        HRESULT hr = S_OK;
        // 1) vsync 設定で present 引数切替
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
            return Result::fail(Facility::D3D12, Code::GettingInfoFailed, Severity::Error, static_cast<uint32_t>(hr), "Failed to present swap chain");
        }
        return Result::ok();
    }
} // 名前空間 cue::graphicscore::dx12
