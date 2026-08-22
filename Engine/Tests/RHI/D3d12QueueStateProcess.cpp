#include <Cue/Foundation/Assert.h>
#include <Cue/RHI/D3D12/TestSupport/D3d12QueueStateProbe.h>

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
    const std::string_view mode = a_arguments[1];

    if (mode == "Smoke")
    {
        cue::Result<cue::D3d12QueueStateProbeReport> result = cue::probe_d3d12_queue_state(assertContext);
        const cue::D3d12QueueStateProbeReport *report = result.try_value();
        return report != nullptr && report->reservedFenceValue == 1 &&
                       report->completedAfterWait >= report->reservedFenceValue &&
                       report->lastSignaledFence == report->reservedFenceValue && report->usedEventWaitPath
                   ? 0
                   : 2;
    }

    if (mode == "Exhaustion")
    {
        return cue::verify_d3d12_fence_exhaustion_for_probe(assertContext) ? 0 : 3;
    }

    if (mode == "WaitRecoveryMatrix")
    {
        return cue::verify_d3d12_queue_fault_for_probe(cue::D3d12QueueFaultProbeMode::WaitRecoveryMatrix, assertContext)
                   ? 0
                   : 5;
    }

    if (mode == "WaitSentinelRefreshRemoval")
    {
        return cue::verify_d3d12_queue_fault_for_probe(cue::D3d12QueueFaultProbeMode::WaitSentinelRefreshRemoval,
                                                       assertContext)
                   ? 0
                   : 23;
    }

    if (mode == "WaitFailedUnavailable")
    {
        const bool valid = cue::verify_d3d12_queue_fault_for_probe(cue::D3d12QueueFaultProbeMode::WaitFailedUnavailable,
                                                                   assertContext);
        std::_Exit(valid ? 0 : 6);
    }

    if (mode == "StaleEventFollowup")
    {
        const bool valid =
            cue::verify_d3d12_queue_fault_for_probe(cue::D3d12QueueFaultProbeMode::StaleEventFollowup, assertContext);
        std::_Exit(valid ? 0 : 14);
    }

    if (mode == "EventCloseFailure")
    {
        const bool valid =
            cue::verify_d3d12_queue_fault_for_probe(cue::D3d12QueueFaultProbeMode::EventCloseFailure, assertContext);
        std::_Exit(valid ? 0 : 7);
    }

    if (mode == "EventCreateFailure")
    {
        const bool valid =
            cue::verify_d3d12_queue_fault_for_probe(cue::D3d12QueueFaultProbeMode::EventCreateFailure, assertContext);
        std::_Exit(valid ? 0 : 8);
    }

    if (mode == "SignalCompletion")
    {
        return cue::verify_d3d12_queue_fault_for_probe(cue::D3d12QueueFaultProbeMode::SignalCompletion, assertContext)
                   ? 0
                   : 9;
    }

    if (mode == "SignalDeviceRemoved")
    {
        return cue::verify_d3d12_queue_fault_for_probe(cue::D3d12QueueFaultProbeMode::SignalDeviceRemoved,
                                                       assertContext)
                   ? 0
                   : 10;
    }

    if (mode == "SignalCompletionDeviceRemovedRace")
    {
        return cue::verify_d3d12_queue_fault_for_probe(cue::D3d12QueueFaultProbeMode::SignalCompletionDeviceRemovedRace,
                                                       assertContext)
                   ? 0
                   : 21;
    }

    if (mode == "SignalUnavailable")
    {
        const bool valid =
            cue::verify_d3d12_queue_fault_for_probe(cue::D3d12QueueFaultProbeMode::SignalUnavailable, assertContext);
        std::_Exit(valid ? 0 : 11);
    }

    if (mode == "SignalEventCloseFailure")
    {
        const bool valid = cue::verify_d3d12_queue_fault_for_probe(
            cue::D3d12QueueFaultProbeMode::SignalEventCloseFailure, assertContext);
        std::_Exit(valid ? 0 : 15);
    }

    if (mode == "SignalEventCreateFailure")
    {
        const bool valid = cue::verify_d3d12_queue_fault_for_probe(
            cue::D3d12QueueFaultProbeMode::SignalEventCreateFailure, assertContext);
        std::_Exit(valid ? 0 : 16);
    }

    if (mode == "TerminalSignalCompletion")
    {
        return cue::verify_d3d12_queue_fault_for_probe(cue::D3d12QueueFaultProbeMode::TerminalSignalCompletion,
                                                       assertContext)
                   ? 0
                   : 17;
    }

    if (mode == "TerminalSignalDeviceRemoved")
    {
        return cue::verify_d3d12_queue_fault_for_probe(cue::D3d12QueueFaultProbeMode::TerminalSignalDeviceRemoved,
                                                       assertContext)
                   ? 0
                   : 18;
    }

    if (mode == "TerminalSignalUnavailable")
    {
        const bool valid = cue::verify_d3d12_queue_fault_for_probe(
            cue::D3d12QueueFaultProbeMode::TerminalSignalUnavailable, assertContext);
        std::_Exit(valid ? 0 : 19);
    }

    if (mode == "TerminalCloseFailure")
    {
        return cue::verify_d3d12_queue_fault_for_probe(cue::D3d12QueueFaultProbeMode::TerminalCloseFailure,
                                                       assertContext)
                   ? 0
                   : 12;
    }

    return 24;
}
