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
    /// @brief DXGI factory と D3D12 device を所有する低レベルデバイス。
    /// @details アダプタ選択、feature level 決定、debug layer 設定をここに集約し、
    ///          他の manager はこのクラスから native object を借りてリソースを作成する。
    class DX12RenderDevice final : public IRenderDevice
    {
    public:
        DX12RenderDevice() = default;
        ~DX12RenderDevice() = default;

        /// @brief D3D12 レンダーデバイスを初期化する
        Result initialize(bool a_enableDebugLayer = false) override;

        /// @brief D3D12 デバイスを取得する
        ID3D12Device* get_d3d12_device() const noexcept { return m_d3d12Device.Get(); }

        [[nodiscard]] bool supports_mesh_shader() const noexcept { return m_supportsMeshShader; }

        /// @brief 選択された GPU アダプタのを取得する
        IDXGIAdapter4* get_adapter() const noexcept { return m_adapter.Get(); }

        /// @brief DXGI ファクトリを取得する
        IDXGIFactory7* get_dxgi_factory() const noexcept { return m_dxgiFactory.Get(); }

    private:
        // --- 内部初期化 ---
        // DXGI factory は swap chain 作成と GPU adapter 列挙の入口になる。
        Result create_dxgi_factory([[maybe_unused]] bool a_enableDebugLayer);

        // 高性能 adapter を選び、利用可能な最大 feature level で ID3D12Device を作る。
        Result create_d3d12_device();
    private:
        ComPtr<IDXGIFactory7> m_dxgiFactory = nullptr; // dxgi ファクトリ
        ComPtr<IDXGIAdapter4> m_adapter = nullptr; // 選択されたアダプタ
        ComPtr<ID3D12Device> m_d3d12Device = nullptr; // d3d12 デバイス
        DXGI_ADAPTER_DESC3 m_adapterDesc = {}; // アダプタ情報
        D3D_FEATURE_LEVEL m_featureLevel = {}; // 機能レベル
        bool m_supportsMeshShader = false;
    };
}
