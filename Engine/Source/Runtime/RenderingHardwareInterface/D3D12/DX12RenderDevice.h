#pragma once

/// ************************************************************************************
/// D3D12レンダーデバイス
/// ************************************************************************************

// === RHI includes ===
#include <RHICommon.h>

// === D3D12 includes ===
#include "DX12Common.h"

namespace Cue::RHI::DX12
{
    class DX12RenderDevice final : public IRenderDevice
    {
    public:
        DX12RenderDevice() = default;
        ~DX12RenderDevice() = default;

        /// @brief D3D12 レンダーデバイスを初期化する
        Result initialize(bool a_enableDebugLayer = false) override;

        /// @brief D3D12 デバイスを取得する
        ID3D12Device* get_d3d12_device() const noexcept { return m_d3d12Device.Get(); }

        /// @brief DXGI ファクトリを取得する
        IDXGIFactory7* get_dxgi_factory() const noexcept { return m_dxgiFactory.Get(); }

    private:
        // --- 内部初期化 ---
        Result create_dxgi_factory([[maybe_unused]] bool a_enableDebugLayer);
        Result create_d3d12_device();
    private:
        comPtr<IDXGIFactory7> m_dxgiFactory = nullptr; // dxgi ファクトリ
        comPtr<ID3D12Device> m_d3d12Device = nullptr; // d3d12 デバイス
        DXGI_ADAPTER_DESC3 m_adapterDesc = {}; // アダプタ情報
        D3D_FEATURE_LEVEL m_featureLevel = {}; // 機能レベル
    };
}
