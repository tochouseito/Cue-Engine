// 選択済み Adapter から D3D12 Device を生成し、Native 失敗を Foundation Error へ変換する内部境界

#pragma once

#include <Cue/Foundation/Result.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

namespace cue
{
class AssertContext;

// Device 生成関数を注入可能にし、実 GPU を必要としない失敗経路検証を可能にする
using D3d12DeviceCreator = HRESULT(WINAPI *)(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);

/// @brief D3D12 Device 生成で使用する D3D12 Device を生成し、呼び出し元へ返す
[[nodiscard]] Result<Microsoft::WRL::ComPtr<ID3D12Device>> create_d3d12_device(
    IDXGIAdapter4 *a_adapter, D3D_FEATURE_LEVEL a_featureLevel,
    const AssertContext &a_assertContext) noexcept;

/// @brief D3D12 Device 生成で使用する D3D12 Device を生成し、呼び出し元へ返す
[[nodiscard]] Result<Microsoft::WRL::ComPtr<ID3D12Device>> create_d3d12_device(
    IDXGIAdapter4 *a_adapter, D3D_FEATURE_LEVEL a_featureLevel, D3d12DeviceCreator a_creator,
    const AssertContext &a_assertContext) noexcept;
} // namespace cue
