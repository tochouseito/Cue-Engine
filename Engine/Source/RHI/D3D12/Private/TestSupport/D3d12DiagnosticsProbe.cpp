#include <Cue/RHI/D3D12/TestSupport/D3d12DiagnosticsProbe.h>

#include "D3d12Diagnostics.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <utility>

namespace
{
/// @brief Probe で収集した状態を呼び出し元が検証可能な Report へまとめる
[[nodiscard]] cue::D3d12DiagnosticsProbeReport make_report(
    const cue::D3d12DiagnosticsStatus &a_status) noexcept
{
    Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
    Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dredSettings;
    cue::D3d12DiagnosticsProbeReport report = {};
    report.isAllowedByBuild = cue::are_d3d12_diagnostics_allowed();
    report.isDebugInterfaceAvailable = SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
    report.isDredInterfaceAvailable = SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings)));
    report.isDebugLayerEnabled = a_status.isDebugLayerEnabled;
    report.isDredEnabled = a_status.isDredEnabled;
    return report;
}
} // namespace

namespace cue
{
/// @brief 現在の Build で D3D12 診断を許可できるか Probe から確認する
bool are_d3d12_diagnostics_allowed_for_probe() noexcept
{
    return are_d3d12_diagnostics_allowed();
}

/// @brief 空の DRED UTF-16 名が既定名へ Fall Back する既存契約を検証する
bool verify_d3d12_empty_dred_name_fallback_for_probe(
    const AssertContext &a_assertContext) noexcept
{
    constexpr std::string_view fallback = "Expected DRED fallback";
    std::string storage;
    const std::string_view selected = select_d3d12_dred_name(
        nullptr, L"", fallback, storage, a_assertContext);
    return selected == fallback && storage.empty();
}

/// @brief 診断無効設定を Device 生成前経路へ適用して結果を返す
Result<D3d12DiagnosticsProbeReport> probe_disabled_d3d12_diagnostics(
    const AssertContext &a_assertContext) noexcept
{
    D3d12BackendDescriptor descriptor = {};
    descriptor.adapterPolicy = D3d12AdapterPolicy::HighPerformanceHardware;
    descriptor.validationMode = D3d12ValidationMode::Disabled;
    descriptor.isDredEnabled = false;
    Result<D3d12DiagnosticsStatus> result =
        configure_d3d12_pre_device_diagnostics(descriptor, a_assertContext);

    if (!result)
    {
        return Result<D3d12DiagnosticsProbeReport>::failure(std::move(*result.try_error()));
    }

    return Result<D3d12DiagnosticsProbeReport>::success(make_report(*result.try_value()));
}

/// @brief Standard Validation と DRED 設定を Device 生成前経路へ適用して結果を返す
Result<D3d12DiagnosticsProbeReport> probe_configured_d3d12_diagnostics(
    const AssertContext &a_assertContext) noexcept
{
    D3d12BackendDescriptor descriptor = {};
    descriptor.adapterPolicy = D3d12AdapterPolicy::HighPerformanceHardware;
    descriptor.validationMode = D3d12ValidationMode::Standard;
    descriptor.isDredEnabled = true;
    Result<D3d12DiagnosticsStatus> result =
        configure_d3d12_pre_device_diagnostics(descriptor, a_assertContext);

    if (!result)
    {
        return Result<D3d12DiagnosticsProbeReport>::failure(std::move(*result.try_error()));
    }

    return Result<D3d12DiagnosticsProbeReport>::success(make_report(*result.try_value()));
}
} // namespace cue
