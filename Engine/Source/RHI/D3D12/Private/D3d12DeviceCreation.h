#pragma once

#include <Cue/Foundation/Result.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

namespace cue
{
class AssertContext;

using D3d12DeviceCreator = HRESULT(WINAPI *)(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);

[[nodiscard]] Result<Microsoft::WRL::ComPtr<ID3D12Device>> create_d3d12_device(
    IDXGIAdapter4 *a_adapter, D3D_FEATURE_LEVEL a_featureLevel,
    const AssertContext &a_assertContext) noexcept;

[[nodiscard]] Result<Microsoft::WRL::ComPtr<ID3D12Device>> create_d3d12_device(
    IDXGIAdapter4 *a_adapter, D3D_FEATURE_LEVEL a_featureLevel, D3d12DeviceCreator a_creator,
    const AssertContext &a_assertContext) noexcept;
} // namespace cue
