#pragma once

#include <Cue/Foundation/Result.h>
#include <Cue/RHI/D3D12/D3d12Backend.h>
#include <Cue/RHI/GraphicsBackend.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace cue
{
class AssertContext;
struct D3d12DiagnosticsStatus;

struct D3d12AdapterCandidateFacts final
{
    bool isSoftware;
    bool supportsRequiredFeatureLevel;
};

struct D3d12AdapterReport final
{
    std::string adapterName;
    std::uint64_t dedicatedVideoMemoryBytes;
    std::uint32_t vendorId;
    std::uint32_t deviceId;
    GraphicsAdapterKind adapterKind;
};

struct D3d12AdapterSelection final
{
    Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter;
    D3d12AdapterReport report;
    D3D_FEATURE_LEVEL featureLevel;
};

using D3d12FactoryCreator = HRESULT(WINAPI *)(UINT, REFIID, void **);

[[nodiscard]] bool is_supported_hardware_candidate(D3d12AdapterCandidateFacts a_candidate) noexcept;

[[nodiscard]] bool should_retry_without_debug_factory(HRESULT a_result, bool a_wasDebugRequested) noexcept;

[[nodiscard]] Result<Microsoft::WRL::ComPtr<IDXGIFactory6>> create_d3d12_factory(
    UINT a_flags, D3d12FactoryCreator a_creator, const AssertContext &a_assertContext) noexcept;

[[nodiscard]] Result<CapabilityReport> make_d3d12_capability_report(
    const D3d12AdapterReport &a_report, bool a_isUma, const AssertContext &a_assertContext) noexcept;

[[nodiscard]] Result<D3d12AdapterSelection> select_d3d12_adapter(
    D3d12AdapterPolicy a_policy, const D3d12DiagnosticsStatus &a_diagnostics,
    const AssertContext &a_assertContext) noexcept;
} // namespace cue
