#pragma once
#include "stdafx.h"

namespace Cue::Graphics::DX12
{
    class RenderDevice
    {
    public:
        RenderDevice() = default;
        ~RenderDevice() = default;
        // 初期化
        [[nodiscard]] Result initialize(bool enableDebugLayer = false);
        // D3D12デバイス取得
        [[nodiscard]] ID3D12Device* get_d3d12_device() const noexcept { return m_d3d12Device.Get(); }
    private:
        // DXGIファクトリ生成
        [[nodiscard]] Result create_dxgi_factory([[maybe_unused]] bool enableDebugLayer);
        // D3D12デバイス生成
        [[nodiscard]] Result create_d3d12_device();
    private:
        ComPtr<IDXGIFactory7> m_dxgiFactory;// DXGIファクトリ
        ComPtr<ID3D12Device> m_d3d12Device; // D3D12デバイス
        DXGI_ADAPTER_DESC3 m_adapterDesc = {};// アダプタ情報
        D3D_FEATURE_LEVEL m_featureLevel = {};// 機能レベル
    };
} // namespace Cue::Graphics::DX12
