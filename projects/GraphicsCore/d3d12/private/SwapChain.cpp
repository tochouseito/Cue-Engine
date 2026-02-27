#include "SwapChain.h"

namespace Cue::GraphicsCore::DX12
{
    Result SwapChain::initialize(HWND hWnd, uint32_t width, uint32_t height, DXGI_FORMAT format, uint32_t bufferCount)
    {
        m_hWnd = hWnd;
        m_desc.Width = width;// 画面の幅。ウィンドウのクライアント領域を同じものにしておく
        m_desc.Height = height;// 画面の高さ。ウィンドウのクライアント領域を同じものにしておく
        m_desc.Format = format;// 色の形式
        m_desc.SampleDesc.Count = 1;// マルチサンプルしない
        m_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;// 描画のターゲットとして利用する
        m_desc.BufferCount = bufferCount;// バッファ数
        m_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;// モニタにうつしたら、中身を破棄
        m_desc.Flags =
            DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING |
            DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;// ティアリングサポート

        HRESULT hr = m_renderDevice.get_dxgi_factory()->CreateSwapChainForHwnd(
            m_queuePool.get_graphics_pool()->get_command_queue(),
            m_hWnd,
            &m_desc,
            nullptr, nullptr,
            reinterpret_cast<IDXGISwapChain1**>(m_swapChain.GetAddressOf()));

        Assert::cue_assert(SUCCEEDED(hr), "Failed to create SwapChain. HRESULT: {:#X}", static_cast<uint32_t>(hr));

        SetDXGIName(m_swapChain.Get(), L"Main SwapChain");

        // リフレッシュレートを取得。floatで取るのは大変なので大体あってれば良いので整数で。
        // ウィンドウがあるモニターを取得
        HMONITOR hMonitor = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFOEX mi{};
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfo(hMonitor, &mi))
        {
            Assert::cue_assert(false, "Failed to get monitor info for refresh rate.");
        }
        // mi.szDeviceに対応するディスプレイ設定を取得
        DEVMODE dm{};
        dm.dmSize = sizeof(dm);
        if (!EnumDisplaySettings(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm))
        {
            Assert::cue_assert(false, "Failed to get display settings for refresh rate.");
        }
        m_refreshrate = static_cast<uint32_t>(dm.dmDisplayFrequency);

        // VSync共存型FPS固定のためにレイテンシ1
        m_swapChain->SetMaximumFrameLatency(1);

        // OSが行うAlt+Enterのフルスクリーンは制御不能なので禁止
        m_renderDevice.get_dxgi_factory()->MakeWindowAssociation(
            m_hWnd,
            DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER);

        // RTV作成
        m_rtvTableIDs.reserve(bufferCount);
        for (uint32_t i = 0; i < bufferCount; ++i)
        {
            ComPtr<ID3D12Resource> pResource;
            hr = m_swapChain->GetBuffer(i, IID_PPV_ARGS(&pResource));
            if (FAILED(hr))
            {
                Assert::cue_assert(false, "Failed to get SwapChain back buffer. HRESULT: {:#X}", static_cast<uint32_t>(hr));
            }
            DescriptorAllocator::TableID rtvTableID = m_descriptorAllocator.allocate(DescriptorAllocator::TableKind::RenderTargets);
            m_descriptorAllocator.create_rtv(rtvTableID, pResource.Get(), format);
            m_rtvTableIDs.push_back(rtvTableID);
        }

        return Result::ok();
    }
    ComPtr<ID3D12Resource> SwapChain::get_back_buffer(uint32_t index) const
    {
        ComPtr <ID3D12Resource> pResource;
        HRESULT hr = m_swapChain->GetBuffer(index, IID_PPV_ARGS(&pResource));
        Assert::cue_assert(SUCCEEDED(hr), "Failed to get SwapChain back buffer. HRESULT: {:#X}", static_cast<uint32_t>(hr));
        return pResource;
    }
    DescriptorAllocator::TableID SwapChain::get_rtv_table_id(uint32_t index) const
    {
        return m_rtvTableIDs[index];
    }
} // namespace Cue::GraphicsCore::DX12
