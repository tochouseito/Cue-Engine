#include <Cue/RHI/D3D12/TestSupport/D3d12AdapterSelectionProbe.h>

#include "D3d12AdapterSelection.h"
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
    Result<CapabilityReport> discreteResult =
        make_d3d12_capability_report(adapter, false, a_assertContext);
    Result<CapabilityReport> umaResult =
        make_d3d12_capability_report(adapter, true, a_assertContext);

    if (!discreteResult || !umaResult)
    {
        return false;
    }

    const CapabilityReport &discrete = *discreteResult.try_value();
    const CapabilityReport &uma = *umaResult.try_value();
    return discrete.adapterName == adapter.adapterName &&
           discrete.dedicatedVideoMemoryBytes == adapter.dedicatedVideoMemoryBytes &&
           discrete.vendorId == adapter.vendorId && discrete.deviceId == adapter.deviceId &&
           discrete.backendKind == GraphicsBackendKind::D3d12 &&
           discrete.adapterKind == GraphicsAdapterKind::Hardware &&
           discrete.profile == GraphicsProfile::Baseline3D && !discrete.isUma && uma.isUma;
}
} // namespace cue
