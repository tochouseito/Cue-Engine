#pragma once
#include "stdafx.h"

namespace Cue::GraphicsCore::DX12
{
    class DX12RenderDevice final : public IRenderDevice
    {
    public:
        DX12RenderDevice() = default;
        ~DX12RenderDevice() = default;
        // 初期化
        Result initialize(bool enableDebugLayer = false) override;
        // d3d12 デバイス取得
        [[nodiscard]] ID3D12Device* get_d3d12_device() const noexcept { return m_d3d12Device.Get(); }
        // dxgi factory 取得
        [[nodiscard]] IDXGIFactory7* get_dxgi_factory() const noexcept { return m_dxgiFactory.Get(); }

    private:
        // dxgi ファクトリ生成
        [[nodiscard]] Result create_dxgi_factory([[maybe_unused]] bool enableDebugLayer);
        // d3d12 デバイス生成
        [[nodiscard]] Result create_d3d12_device();
    private:
        ComPtr<IDXGIFactory7> m_dxgiFactory; // dxgi ファクトリ
        ComPtr<ID3D12Device> m_d3d12Device; // d3d12 デバイス
        DXGI_ADAPTER_DESC3 m_adapterDesc = {}; // アダプタ情報
        D3D_FEATURE_LEVEL m_featureLevel = {}; // 機能レベル
    };
} // 名前空間 cue::graphicscore::dx12
