#include <Cue/RHI/D3D12/TestSupport/D3d12AdapterSelectionProbe.h>

#include "D3d12AdapterSelection.h"
#include "D3d12CapabilityQuery.h"
#include "D3d12Diagnostics.h"

#include <array>
#include <utility>

namespace
{
struct FactoryProbeState final
{
    bool requestedDebugFactory;
    bool retriedWithoutDebug;
};

thread_local FactoryProbeState g_factoryProbeState = {};

/// @brief D3D12 Adapter 選択 Probe で使用する Factory For Probe を生成し、呼び出し元へ返す
HRESULT WINAPI create_factory_for_probe(UINT a_flags, REFIID a_interfaceId, void **a_factory) noexcept
{
    if (a_flags == DXGI_CREATE_FACTORY_DEBUG)
    {
        g_factoryProbeState.requestedDebugFactory = true;
        return DXGI_ERROR_SDK_COMPONENT_MISSING;
    }

    g_factoryProbeState.retriedWithoutDebug = true;
    return CreateDXGIFactory2(a_flags, a_interfaceId, a_factory);
}
} // namespace

namespace cue
{
Result<D3d12AdapterSelectionProbeReport> probe_d3d12_adapter_selection(
    D3d12AdapterPolicy a_policy, const AssertContext &a_assertContext) noexcept
{
    D3d12DiagnosticsStatus diagnostics = {};
    Result<D3d12AdapterSelection> selectionResult =
        select_d3d12_adapter(a_policy, diagnostics, a_assertContext);

    if (!selectionResult)
    {
        return Result<D3d12AdapterSelectionProbeReport>::failure(
            std::move(*selectionResult.try_error()));
    }

    D3d12AdapterSelection &selection = *selectionResult.try_value();
    D3d12AdapterSelectionProbeReport report = {};
    report.adapterName = std::move(selection.report.adapterName);
    report.dedicatedVideoMemoryBytes = selection.report.dedicatedVideoMemoryBytes;
    report.vendorId = selection.report.vendorId;
    report.deviceId = selection.report.deviceId;
    report.adapterKind = selection.report.adapterKind;
    report.isFeatureLevel12_0 = selection.featureLevel == D3D_FEATURE_LEVEL_12_0;
    return Result<D3d12AdapterSelectionProbeReport>::success(std::move(report));
}

bool verify_d3d12_unsupported_candidate_skip_for_probe() noexcept
{
    constexpr std::array<D3d12AdapterCandidateFacts, 3> candidates = {{
        {false, false},
        {true, true},
        {false, true},
    }};
    std::size_t selectedIndex = candidates.size();

    for (std::size_t index = 0; index < candidates.size(); ++index)
    {
        if (is_supported_hardware_candidate(candidates[index]))
        {
            selectedIndex = index;
            break;
        }
    }

    return selectedIndex == 2;
}

/// @brief 空の DXGI Adapter 名が既存の D3D12 Error Code 23 で拒否されることを検証する
bool verify_d3d12_empty_adapter_name_rejection_for_probe(
    const AssertContext &a_assertContext) noexcept
{
    Result<std::string> result = convert_d3d12_adapter_name({}, a_assertContext);
    const Error *error = result.try_error();
    return !result && error != nullptr && error->code().domain() == "Cue.RHI.D3D12" &&
           error->code().value() == 23;
}

Result<D3d12FactoryFallbackProbeReport> probe_d3d12_factory_fallback(
    const AssertContext &a_assertContext) noexcept
{
    g_factoryProbeState = {};
    Result<Microsoft::WRL::ComPtr<IDXGIFactory6>> factoryResult = create_d3d12_factory(
        DXGI_CREATE_FACTORY_DEBUG, create_factory_for_probe, a_assertContext);

    if (!factoryResult)
    {
        return Result<D3d12FactoryFallbackProbeReport>::failure(std::move(*factoryResult.try_error()));
    }

    D3d12FactoryFallbackProbeReport report = {};
    report.requestedDebugFactory = g_factoryProbeState.requestedDebugFactory;
    report.retriedWithoutDebug = g_factoryProbeState.retriedWithoutDebug;
    return Result<D3d12FactoryFallbackProbeReport>::success(std::move(report));
}

bool verify_d3d12_capability_mapping_for_probe(const AssertContext &a_assertContext) noexcept
{
    D3d12AdapterReport adapter = {};
    adapter.adapterName = "Synthetic Adapter";
    adapter.dedicatedVideoMemoryBytes = 1024;
    adapter.vendorId = 1;
    adapter.deviceId = 2;
    adapter.adapterKind = GraphicsAdapterKind::Hardware;
    Result<CapabilityReport> reportResult = make_d3d12_capability_report(adapter, a_assertContext);

    if (!reportResult)
    {
        return false;
    }

    const CapabilityReport &report = *reportResult.try_value();
    return report.adapterName == adapter.adapterName &&
           report.dedicatedVideoMemoryBytes == adapter.dedicatedVideoMemoryBytes &&
           report.vendorId == adapter.vendorId && report.deviceId == adapter.deviceId &&
           report.backendKind == GraphicsBackendKind::D3d12 &&
           report.adapterKind == GraphicsAdapterKind::Hardware &&
           report.profile == GraphicsProfile::Baseline3D;
}

bool verify_d3d12_optional_capability_failure_for_probe(AssertContext &a_assertContext) noexcept
{
    struct FailureReset final
    {
        /// @brief Probe終了時にOptional Capability失敗注入を必ず解除する
        ~FailureReset() noexcept
        {
            d3d12_private::clear_capability_query_failure_for_probe();
        }
    } reset;
    static_cast<void>(reset);

    d3d12_private::set_capability_query_failure_for_probe(D3D12_FEATURE_D3D12_OPTIONS7);
    D3d12BackendDescriptor descriptor = {
        D3d12AdapterPolicy::Warp,
        D3d12ValidationMode::Disabled,
        false,
        5'000,
    };
    Result<std::unique_ptr<D3d12Backend>> backendResult =
        create_d3d12_backend(descriptor, a_assertContext);
    if (!backendResult)
    {
        return false;
    }

    std::unique_ptr<D3d12Backend> backend = std::move(*backendResult.try_value());
    const CapabilityReport &capabilities = backend->capabilities();
    const bool valid = capabilities.meshShader.support_state().query_status() == CapabilityQueryStatus::Failed &&
                       capabilities.meshShader.support_state().support() == CapabilitySupport::Unknown &&
                       capabilities.samplerFeedback.support_state().query_status() == CapabilityQueryStatus::Failed &&
                       backend->state() == GraphicsBackendState::Ready;
    Result<void> shutdownResult = backend->shutdown();
    return valid && shutdownResult;
}

bool verify_d3d12_optional_capability_log_failure_for_probe(AssertContext &a_assertContext) noexcept
{
    struct FailureReset final
    {
        /// @brief Probe終了時にOptional Capability失敗注入を必ず解除する
        ~FailureReset() noexcept
        {
            d3d12_private::clear_capability_query_failure_for_probe();
        }
    } reset;
    static_cast<void>(reset);

    d3d12_private::set_capability_query_failure_for_probe(D3D12_FEATURE_D3D12_OPTIONS7);
    D3d12BackendDescriptor descriptor = {
        D3d12AdapterPolicy::Warp,
        D3d12ValidationMode::Disabled,
        false,
        5'000,
    };
    Result<std::unique_ptr<D3d12Backend>> backendResult = create_d3d12_backend(descriptor, a_assertContext);
    const Error *error = backendResult.try_error();
    return !backendResult && error != nullptr && error->code().domain() == "Cue.RHI.D3D12" &&
           error->code().value() == 101;
}

bool verify_d3d12_required_capability_failure_for_probe(AssertContext &a_assertContext) noexcept
{
    struct FailureReset final
    {
        /// @brief Probe終了時にRequired Capability失敗注入を必ず解除する
        ~FailureReset() noexcept
        {
            d3d12_private::clear_capability_query_failure_for_probe();
        }
    } reset;
    static_cast<void>(reset);

    d3d12_private::set_capability_query_failure_for_probe(D3D12_FEATURE_FEATURE_LEVELS);
    D3d12BackendDescriptor descriptor = {
        D3d12AdapterPolicy::Warp,
        D3d12ValidationMode::Disabled,
        false,
        5'000,
    };
    Result<std::unique_ptr<D3d12Backend>> backendResult = create_d3d12_backend(descriptor, a_assertContext);
    const Error *error = backendResult.try_error();
    return !backendResult && error != nullptr && error->code().domain() == "Cue.RHI.D3D12" &&
           error->code().value() == 102;
}

bool verify_d3d12_unknown_capability_value_for_probe(AssertContext &a_assertContext) noexcept
{
    struct UnknownValueReset final
    {
        /// @brief Probe終了時に未知Capability値注入を必ず解除する
        ~UnknownValueReset() noexcept
        {
            d3d12_private::clear_capability_query_unknown_value_for_probe();
        }
    } reset;
    static_cast<void>(reset);

    d3d12_private::set_capability_query_unknown_value_for_probe(D3D12_FEATURE_D3D12_OPTIONS7);
    D3d12BackendDescriptor descriptor = {
        D3d12AdapterPolicy::Warp,
        D3d12ValidationMode::Disabled,
        false,
        5'000,
    };
    Result<std::unique_ptr<D3d12Backend>> backendResult = create_d3d12_backend(descriptor, a_assertContext);
    if (!backendResult)
    {
        return false;
    }

    std::unique_ptr<D3d12Backend> backend = std::move(*backendResult.try_value());
    const CapabilityReport &capabilities = backend->capabilities();
    const bool valid = capabilities.meshShader.support_state().query_status() == CapabilityQueryStatus::Failed &&
                       capabilities.meshShader.support_state().support() == CapabilitySupport::Unknown &&
                       backend->state() == GraphicsBackendState::Ready;
    Result<void> shutdownResult = backend->shutdown();
    return valid && shutdownResult;
}

bool verify_d3d12_unknown_capability_value_log_failure_for_probe(AssertContext &a_assertContext) noexcept
{
    struct UnknownValueReset final
    {
        /// @brief Probe終了時に未知Capability値注入を必ず解除する
        ~UnknownValueReset() noexcept
        {
            d3d12_private::clear_capability_query_unknown_value_for_probe();
        }
    } reset;
    static_cast<void>(reset);

    d3d12_private::set_capability_query_unknown_value_for_probe(D3D12_FEATURE_D3D12_OPTIONS7);
    D3d12BackendDescriptor descriptor = {
        D3d12AdapterPolicy::Warp,
        D3d12ValidationMode::Disabled,
        false,
        5'000,
    };
    Result<std::unique_ptr<D3d12Backend>> backendResult = create_d3d12_backend(descriptor, a_assertContext);
    const Error *error = backendResult.try_error();
    return !backendResult && error != nullptr && error->code().domain() == "Cue.RHI.D3D12" &&
           error->code().value() == 101;
}

bool verify_d3d12_legacy_feature_level_runtime_for_probe(AssertContext &a_assertContext) noexcept
{
    struct LegacyRuntimeReset final
    {
        /// @brief Probe終了時に旧Runtime再現を必ず解除する
        ~LegacyRuntimeReset() noexcept
        {
            d3d12_private::disable_legacy_feature_level_runtime_for_probe();
        }
    } reset;
    static_cast<void>(reset);

    d3d12_private::enable_legacy_feature_level_runtime_for_probe();
    D3d12BackendDescriptor descriptor = {
        D3d12AdapterPolicy::Warp,
        D3d12ValidationMode::Disabled,
        false,
        5'000,
    };
    Result<std::unique_ptr<D3d12Backend>> backendResult = create_d3d12_backend(descriptor, a_assertContext);
    if (!backendResult)
    {
        return false;
    }

    std::unique_ptr<D3d12Backend> backend = std::move(*backendResult.try_value());
    const CapabilityReport &capabilities = backend->capabilities();
    const bool valid = capabilities.featureLevel.support_state().query_status() == CapabilityQueryStatus::Succeeded &&
                       capabilities.featureLevel.support_state().support() == CapabilitySupport::Supported &&
                       backend->state() == GraphicsBackendState::Ready;
    Result<void> shutdownResult = backend->shutdown();
    return valid && shutdownResult;
}
} // namespace cue
