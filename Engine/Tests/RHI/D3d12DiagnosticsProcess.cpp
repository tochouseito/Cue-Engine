#include <Cue/Foundation/Assert.h>
#include <Cue/RHI/D3D12/TestSupport/D3d12DiagnosticsProbe.h>

#include <cstdlib>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
class ProcessFatalHandler final : public cue::FatalHandler
{
  public:
    [[noreturn]] void terminate() noexcept override
    {
        std::_Exit(90);
    }

    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::_Exit(91);
    }
};

class ProcessLogSink final : public cue::LogSink
{
  public:
    [[nodiscard]] bool write(const cue::LogRecord &) override
    {
        return true;
    }

    [[nodiscard]] bool flush() override
    {
        return true;
    }
};

[[nodiscard]] int validate_disabled(const cue::D3d12DiagnosticsProbeReport &a_report) noexcept
{
    return !a_report.isDebugLayerEnabled && !a_report.isDredEnabled ? 0 : 10;
}

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

int main(int a_argumentCount, char **a_arguments)
{
    if (a_argumentCount != 2)
    {
        return 1;
    }

    ProcessFatalHandler fatalHandler;
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    sinks.push_back(std::make_unique<ProcessLogSink>());
    cue::Logger logger(fatalHandler, std::move(sinks));
    cue::AssertContext assertContext(logger, fatalHandler);
    std::string_view mode = a_arguments[1];
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
