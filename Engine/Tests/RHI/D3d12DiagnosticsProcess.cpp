#include "TestSupport/RhiProcessTestFixture.h"

#include <Cue/Foundation/Assert.h>
#include <Cue/RHI/D3D12/TestSupport/D3d12DiagnosticsProbe.h>

#include <string_view>

namespace
{
/// @brief D3d12DiagnosticsProcess Test の Disabled が期待する契約を満たすか検証する
[[nodiscard]] int validate_disabled(const cue::D3d12DiagnosticsProbeReport &a_report) noexcept
{
    return !a_report.isDebugLayerEnabled && !a_report.isDredEnabled ? 0 : 10;
}

/// @brief D3d12DiagnosticsProcess Test の Configured が期待する契約を満たすか検証する
[[nodiscard]] int validate_configured(const cue::D3d12DiagnosticsProbeReport &a_report) noexcept
{
    if (!a_report.isAllowedByBuild)
    {
        return 11;
    }

    if (a_report.isDebugLayerEnabled != a_report.isDebugInterfaceAvailable)
    {
        return 12;
    }

    return a_report.isDredEnabled == a_report.isDredInterfaceAvailable ? 0 : 13;
}
} // namespace

/// @brief 指定 Scenario で D3D12 診断機能を実行し、Info Queue と Error 変換を Process 単位で検証する
int main(int a_argumentCount, char **a_arguments)
{
    if (a_argumentCount != 2)
    {
        return 1;
    }

    cue::test::RhiProcessTestFixture fixture;
    cue::AssertContext &assertContext = fixture.assert_context();
    std::string_view mode = a_arguments[1];

    if (mode == "EmptyDredFallback")
    {
        return cue::verify_d3d12_empty_dred_name_fallback_for_probe(assertContext) ? 0 : 5;
    }

    cue::Result<cue::D3d12DiagnosticsProbeReport> result =
        mode == "Disabled" ? cue::probe_disabled_d3d12_diagnostics(assertContext)
                           : cue::probe_configured_d3d12_diagnostics(assertContext);

    if (mode != "Disabled" && mode != "Configured")
    {
        return 2;
    }

    if (mode == "Configured" && !result)
    {
        return cue::are_d3d12_diagnostics_allowed_for_probe() ? 3 : 0;
    }

    if (!result)
    {
        return 4;
    }

    return mode == "Disabled" ? validate_disabled(*result.try_value()) : validate_configured(*result.try_value());
}
