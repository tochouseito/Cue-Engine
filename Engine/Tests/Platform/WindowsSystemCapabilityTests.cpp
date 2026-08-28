#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Fatal.h>
#include <Cue/Foundation/Log.h>
#include <Cue/Platform/Windows/TestSupport/WindowsSystemCapabilityProbe.h>
#include <Cue/Platform/Windows/WindowsPlatform.h>

#include <cstdlib>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
class TestFatalHandler final : public cue::FatalHandler
{
  public:
    /// @brief 回復不能なTest失敗時にProcessを即時終了する
    [[noreturn]] void terminate() noexcept override
    {
        std::_Exit(75);
    }

    /// @brief 診断Message付きの回復不能なTest失敗時にProcessを即時終了する
    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::_Exit(76);
    }
};
} // namespace

/// @brief Synthetic Mappingと現在MachineのSystem Capability Query契約を検証する
int main()
{
    if (!cue::verify_windows_system_capability_mapping_for_probe())
    {
        return 1;
    }

    TestFatalHandler fatalHandler;
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    cue::Logger logger(fatalHandler, std::move(sinks));
    cue::AssertContext assertContext(logger, fatalHandler);
    const cue::SystemCapabilityQueryReport report = cue::query_windows_system_capabilities(assertContext);
    const std::uint32_t *logicalProcessorCount = report.snapshot.logical_processor_count().try_value();
    const std::uint32_t *pageSize = report.snapshot.page_size_bytes().try_value();

    return report.diagnosticResult == cue::LogResult::Success && logicalProcessorCount != nullptr &&
                   *logicalProcessorCount > 0 && pageSize != nullptr && *pageSize > 0 &&
                   report.snapshot.instructions().sse2.query_status() == cue::CapabilityQueryStatus::Succeeded
               ? 0
               : 2;
}
