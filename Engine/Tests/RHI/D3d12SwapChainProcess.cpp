#include <Cue/Foundation/Assert.h>
#include <Cue/Platform/WindowSystem.h>
#include <Cue/Platform/Windows/WindowsPlatform.h>
#include <Cue/Platform/Windows/WindowsWindowInterop.h>
#include <Cue/RHI/D3D12/D3d12Backend.h>
#include <Cue/RHI/D3D12/TestSupport/D3d12DiagnosticsProbe.h>
#include <Cue/RHI/D3D12/TestSupport/D3d12SwapChainProbe.h>
#include <Cue/RHI/D3D12/Windows/D3d12WindowsPresentation.h>

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
    [[nodiscard]] bool write(const cue::LogRecord &a_record) override
    {
        if (a_record.level() == cue::LogLevel::Error || a_record.level() == cue::LogLevel::Fatal)
        {
            ++m_errorCount;
        }

        return m_isEnabled;
    }

    [[nodiscard]] bool flush() override
    {
        return true;
    }

    void set_enabled(bool a_isEnabled) noexcept
    {
        m_isEnabled = a_isEnabled;
    }

    [[nodiscard]] std::uint32_t error_count() const noexcept
    {
        return m_errorCount;
    }

  private:
    std::uint32_t m_errorCount = 0;
    bool m_isEnabled = true;
};

class FailingLogSink final : public cue::LogSink
{
  public:
    [[nodiscard]] bool write(const cue::LogRecord &) override
    {
        return false;
    }

    [[nodiscard]] bool flush() override
    {
        return true;
    }
};

class ForeignWindow final : public cue::Window
{
  public:
    [[nodiscard]] cue::Result<void> show() noexcept override
    {
        return cue::Result<void>::success();
    }

    [[nodiscard]] cue::Result<void> destroy() noexcept override
    {
        return cue::Result<void>::success();
    }

    [[nodiscard]] cue::WindowState state() const noexcept override
    {
        return cue::WindowState::Created;
    }

    [[nodiscard]] cue::WindowSize client_size() const noexcept override
    {
        return {320, 180};
    }

    [[nodiscard]] bool try_pop_event(cue::WindowEvent &) noexcept override
    {
        return false;
    }
};

[[nodiscard]] bool has_error_code(const cue::Error *a_error, std::int64_t a_value) noexcept
{
    return a_error != nullptr && a_error->code().domain() == "Cue.RHI.D3D12" && a_error->code().value() == a_value;
}

[[nodiscard]] bool has_platform_error_code(const cue::Error *a_error, std::int64_t a_value) noexcept
{
    return a_error != nullptr && a_error->code().domain() == "Cue.Platform.Windows" &&
           a_error->code().value() == a_value;
}

[[nodiscard]] bool run_production_ownership(cue::Window &a_window, cue::AssertContext &a_assertContext) noexcept
{
    cue::D3d12BackendDescriptor backendDescriptor = {
        cue::D3d12AdapterPolicy::Warp,
        cue::D3d12ValidationMode::Disabled,
        false,
        5'000,
    };
    cue::Result<std::unique_ptr<cue::D3d12Backend>> backendResult =
        cue::create_d3d12_backend(backendDescriptor, a_assertContext);

    if (!backendResult)
    {
        return false;
    }

    std::unique_ptr<cue::D3d12Backend> backend = std::move(*backendResult.try_value());
    cue::PresentationDescriptor presentationDescriptor = {true};
    ForeignWindow foreignWindow;
    cue::Result<std::unique_ptr<cue::PresentationContext>> foreignResult =
        cue::create_d3d12_windows_presentation(*backend, foreignWindow, presentationDescriptor);

    if (foreignResult || !has_platform_error_code(foreignResult.try_error(), 12) ||
        backend->state() != cue::GraphicsBackendState::Ready)
    {
        static_cast<void>(backend->shutdown());
        return false;
    }

    cue::Result<std::unique_ptr<cue::PresentationContext>> presentationResult =
        cue::create_d3d12_windows_presentation(*backend, a_window, presentationDescriptor);

    if (!presentationResult)
    {
        static_cast<void>(backend->shutdown());
        return false;
    }

    std::unique_ptr<cue::PresentationContext> presentation = std::move(*presentationResult.try_value());
    cue::Result<void> activeGateResult = backend->shutdown();
    const bool valid = presentation->state() == cue::PresentationContextState::Ready && presentation->width() == 640 &&
                       presentation->height() == 360 && presentation->buffer_count() == 2 &&
                       presentation->current_back_buffer_index() < 2 && presentation->is_vsync_enabled() &&
                       !presentation->is_tearing_enabled() && !activeGateResult &&
                       has_error_code(activeGateResult.try_error(), 87) &&
                       backend->state() == cue::GraphicsBackendState::Ready;
    cue::Result<void> presentationShutdownResult = presentation->shutdown();

    if (!presentationShutdownResult)
    {
        return false;
    }

    presentation.reset();
    cue::Result<void> backendShutdownResult = backend->shutdown();

    if (!backendShutdownResult)
    {
        return false;
    }

    backend.reset();
    return valid;
}

[[nodiscard]] bool run_resize_lifecycle(cue::Window &a_window, ProcessLogSink &a_logSink,
                                        cue::AssertContext &a_assertContext) noexcept
{
    const std::uint32_t initialErrorCount = a_logSink.error_count();
    cue::D3d12BackendDescriptor backendDescriptor = {
        cue::D3d12AdapterPolicy::Warp,
        cue::are_d3d12_diagnostics_allowed_for_probe() ? cue::D3d12ValidationMode::Standard
                                                       : cue::D3d12ValidationMode::Disabled,
        false,
        5'000,
    };
    cue::Result<std::unique_ptr<cue::D3d12Backend>> backendResult =
        cue::create_d3d12_backend(backendDescriptor, a_assertContext);

    if (!backendResult)
    {
        return false;
    }

    std::unique_ptr<cue::D3d12Backend> backend = std::move(*backendResult.try_value());
    cue::PresentationDescriptor descriptor = {true};
    cue::Result<std::unique_ptr<cue::PresentationContext>> presentationResult =
        cue::create_d3d12_windows_presentation(*backend, a_window, descriptor);

    if (!presentationResult)
    {
        static_cast<void>(backend->shutdown());
        return false;
    }

    std::unique_ptr<cue::PresentationContext> presentation = std::move(*presentationResult.try_value());
    cue::D3d12PresentationProbeReport initialReport = cue::probe_d3d12_presentation(*presentation);
    bool valid = initialReport.rtvCount == 2 && initialReport.formatsMatch && initialReport.isAcceptingFrames;
    valid = valid && presentation->resize(640, 360) && !presentation->is_resize_pending();
    valid = valid && presentation->resize(0, 360) && presentation->is_resize_pending() &&
            presentation->width() == 640 && presentation->height() == 360;
    cue::D3d12PresentationProbeReport suspendedReport = cue::probe_d3d12_presentation(*presentation);
    valid = valid && suspendedReport.rtvCount == 2 && suspendedReport.formatsMatch &&
            !suspendedReport.isAcceptingFrames;
    valid = valid && presentation->resize(640, 360) && !presentation->is_resize_pending();

    for (std::uint32_t iteration = 0; iteration < 50 && valid; ++iteration)
    {
        const std::uint32_t width = 641 + iteration;
        const std::uint32_t height = 361 + (iteration % 7);
        cue::Result<void> resizeResult = presentation->resize(width, height);
        cue::D3d12PresentationProbeReport report = cue::probe_d3d12_presentation(*presentation);
        valid = resizeResult && presentation->state() == cue::PresentationContextState::Ready &&
                presentation->width() == width && presentation->height() == height &&
                presentation->current_back_buffer_index() < 2 && report.rtvCount == 2 && report.formatsMatch &&
                report.isAcceptingFrames;
    }

    cue::Result<void> presentationShutdownResult = presentation->shutdown();
    presentation.reset();
    cue::Result<void> backendShutdownResult = backend->shutdown();
    valid = valid && presentationShutdownResult && backendShutdownResult &&
            backend->state() == cue::GraphicsBackendState::Shutdown &&
            a_logSink.error_count() == initialErrorCount;
    backend.reset();
    return valid;
}

[[nodiscard]] int run_device_removal_lifecycle(cue::Window &a_window, cue::AssertContext &a_assertContext) noexcept
{
    cue::D3d12BackendDescriptor backendDescriptor = {
        cue::D3d12AdapterPolicy::Warp,
        cue::D3d12ValidationMode::Disabled,
        false,
        5'000,
    };
    cue::Result<std::unique_ptr<cue::D3d12Backend>> backendResult =
        cue::create_d3d12_backend(backendDescriptor, a_assertContext);

    if (!backendResult)
    {
        return 13;
    }

    std::unique_ptr<cue::D3d12Backend> backend = std::move(*backendResult.try_value());
    cue::PresentationDescriptor descriptor = {true};
    cue::Result<std::unique_ptr<cue::PresentationContext>> presentationResult =
        cue::create_d3d12_windows_presentation(*backend, a_window, descriptor);

    if (!presentationResult)
    {
        static_cast<void>(backend->shutdown());
        return 14;
    }

    std::unique_ptr<cue::PresentationContext> presentation = std::move(*presentationResult.try_value());
    cue::Result<void> removalResult = cue::remove_d3d12_device_without_classification_for_probe(*backend);

    if (has_error_code(removalResult.try_error(), 89))
    {
        static_cast<void>(presentation->shutdown());
        presentation.reset();
        static_cast<void>(backend->shutdown());
        backend.reset();
        return 77;
    }

    cue::Result<std::uint32_t> firstCountResult = cue::d3d12_dred_attempt_count_for_probe(*backend);
    const bool unclassifiedRemovalValid = removalResult && firstCountResult && *firstCountResult.try_value() == 0 &&
                                          backend->state() == cue::GraphicsBackendState::Ready;
    cue::Result<void> presentationShutdownResult = presentation->shutdown();
    cue::Result<std::uint32_t> contextCountResult = cue::d3d12_dred_attempt_count_for_probe(*backend);
    const bool contextValid = presentationShutdownResult &&
                              presentation->state() == cue::PresentationContextState::Shutdown &&
                              backend->state() == cue::GraphicsBackendState::DeviceRemoved && contextCountResult &&
                              *contextCountResult.try_value() == 1;
    presentation.reset();
    cue::Result<void> backendShutdownResult = backend->shutdown();
    const bool backendValid = backendShutdownResult && backend->state() == cue::GraphicsBackendState::Shutdown;
    backend.reset();
    return unclassifiedRemovalValid && contextValid && backendValid ? 0 : 15;
}

[[nodiscard]] int run_device_removal_resize(cue::Window &a_window, cue::AssertContext &a_assertContext) noexcept
{
    cue::D3d12BackendDescriptor backendDescriptor = {
        cue::D3d12AdapterPolicy::Warp,
        cue::D3d12ValidationMode::Disabled,
        false,
        5'000,
    };
    cue::Result<std::unique_ptr<cue::D3d12Backend>> backendResult =
        cue::create_d3d12_backend(backendDescriptor, a_assertContext);

    if (!backendResult)
    {
        return 22;
    }

    std::unique_ptr<cue::D3d12Backend> backend = std::move(*backendResult.try_value());
    cue::PresentationDescriptor descriptor = {true};
    cue::Result<std::unique_ptr<cue::PresentationContext>> presentationResult =
        cue::create_d3d12_windows_presentation(*backend, a_window, descriptor);

    if (!presentationResult)
    {
        static_cast<void>(backend->shutdown());
        return 23;
    }

    std::unique_ptr<cue::PresentationContext> presentation = std::move(*presentationResult.try_value());
    cue::Result<void> removalResult = cue::remove_d3d12_device_without_classification_for_probe(*backend);

    if (has_error_code(removalResult.try_error(), 89))
    {
        static_cast<void>(presentation->shutdown());
        presentation.reset();
        static_cast<void>(backend->shutdown());
        return 77;
    }

    cue::Result<std::uint32_t> firstCountResult = cue::d3d12_dred_attempt_count_for_probe(*backend);
    cue::Result<void> resizeResult = presentation->resize(641, 361);
    cue::Result<std::uint32_t> resizeCountResult = cue::d3d12_dred_attempt_count_for_probe(*backend);
    cue::Result<cue::D3d12DredOwnerProbeReport> dredOwnerResult =
        cue::probe_d3d12_dred_owners_for_probe(*backend);
    const cue::D3d12DredOwnerProbeReport *dredOwners = dredOwnerResult.try_value();
    const bool resizeValid = removalResult && firstCountResult && *firstCountResult.try_value() == 0 &&
                             !resizeResult && has_error_code(resizeResult.try_error(), 34) && resizeCountResult &&
                             *resizeCountResult.try_value() == 1 &&
                             dredOwners != nullptr && dredOwners->hasCommandList &&
                             dredOwners->allocatorCount == 2 && dredOwners->backBufferCount == 2 &&
                             dredOwners->rtvCount == 2 &&
                             dredOwners->hasSwapChain && dredOwners->hasRtvHeap && dredOwners->hasQueue &&
                             dredOwners->hasFence && dredOwners->hasFenceEvent &&
                             presentation->state() == cue::PresentationContextState::Shutdown &&
                             backend->state() == cue::GraphicsBackendState::DeviceRemoved;
    cue::Result<void> presentationShutdownResult = presentation->shutdown();
    presentation.reset();
    cue::Result<void> backendShutdownResult = backend->shutdown();
    const bool cleanupValid = presentationShutdownResult && backendShutdownResult &&
                              backend->state() == cue::GraphicsBackendState::Shutdown;
    backend.reset();
    return resizeValid && cleanupValid ? 0 : 24;
}

[[nodiscard]] int run_device_removal_dred_failure(cue::Window &a_window, ProcessLogSink &a_logSink,
                                                  cue::AssertContext &a_assertContext) noexcept
{
    if (!cue::are_d3d12_diagnostics_allowed_for_probe())
    {
        return 77;
    }

    cue::D3d12BackendDescriptor backendDescriptor = {
        cue::D3d12AdapterPolicy::Warp,
        cue::D3d12ValidationMode::Standard,
        true,
        5'000,
    };
    cue::Result<std::unique_ptr<cue::D3d12Backend>> backendResult =
        cue::create_d3d12_backend(backendDescriptor, a_assertContext);

    if (!backendResult)
    {
        return 16;
    }

    std::unique_ptr<cue::D3d12Backend> backend = std::move(*backendResult.try_value());
    cue::PresentationDescriptor descriptor = {true};
    cue::Result<std::unique_ptr<cue::PresentationContext>> presentationResult =
        cue::create_d3d12_windows_presentation(*backend, a_window, descriptor);

    if (!presentationResult)
    {
        static_cast<void>(backend->shutdown());
        return 17;
    }

    std::unique_ptr<cue::PresentationContext> presentation = std::move(*presentationResult.try_value());
    cue::Result<void> removalResult = cue::remove_d3d12_device_without_classification_for_probe(*backend);
    cue::Result<std::uint32_t> firstCountResult = cue::d3d12_dred_attempt_count_for_probe(*backend);
    const bool removalValid = removalResult && firstCountResult && *firstCountResult.try_value() == 0 &&
                              backend->state() == cue::GraphicsBackendState::Ready;
    a_logSink.set_enabled(false);
    cue::Result<void> presentationShutdownResult = presentation->shutdown();
    a_logSink.set_enabled(true);
    cue::Result<std::uint32_t> contextCountResult = cue::d3d12_dred_attempt_count_for_probe(*backend);
    const bool contextValid = !presentationShutdownResult &&
                              presentation->state() == cue::PresentationContextState::Shutdown &&
                              backend->state() == cue::GraphicsBackendState::DeviceRemoved && contextCountResult &&
                              *contextCountResult.try_value() == 1;
    presentation.reset();
    cue::Result<void> backendShutdownResult = backend->shutdown();
    const bool cleanupValid =
        backendShutdownResult && backend->state() == cue::GraphicsBackendState::Shutdown;
    backend.reset();
    return removalValid && contextValid && cleanupValid ? 0 : 18;
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
    std::unique_ptr<ProcessLogSink> processSink = std::make_unique<ProcessLogSink>();
    ProcessLogSink *processSinkView = processSink.get();
    sinks.push_back(std::move(processSink));
    cue::Logger logger(fatalHandler, std::move(sinks));
    cue::AssertContext assertContext(logger, fatalHandler);
    cue::Result<std::unique_ptr<cue::WindowSystem>> systemResult = cue::create_windows_window_system(assertContext);

    if (!systemResult)
    {
        return 2;
    }

    std::unique_ptr<cue::WindowSystem> windowSystem = std::move(*systemResult.try_value());
    cue::WindowDescriptor windowDescriptor = {"CueEngine D3D12 Swap Chain Probe", {640, 360}};
    cue::Result<std::unique_ptr<cue::Window>> windowResult = windowSystem->create_window(windowDescriptor);

    if (!windowResult)
    {
        return 3;
    }

    std::unique_ptr<cue::Window> window = std::move(*windowResult.try_value());
    cue::Result<cue::NativeWindowView> nativeWindowResult = cue::get_native_window_view(*window, assertContext);

    if (!nativeWindowResult)
    {
        static_cast<void>(window->destroy());
        return 4;
    }

    const void *nativeWindow = nativeWindowResult.try_value()->value();
    const std::string_view mode = a_arguments[1];
    bool valid = false;
    int failureCode = 10;

    if (mode == "SmokeVsync" || mode == "SmokeImmediate" || mode == "InfoQueue")
    {
        const bool isVsyncEnabled = mode != "SmokeImmediate";
        cue::Result<cue::D3d12SwapChainProbeReport> result =
            cue::probe_d3d12_swap_chain(nativeWindow, 640, 360, isVsyncEnabled, assertContext);
        const cue::D3d12SwapChainProbeReport *report = result.try_value();

        if (mode == "InfoQueue" && report != nullptr && !report->diagnosticsAvailable)
        {
            valid = true;
            failureCode = 77;
        }
        else
        {
            valid = report != nullptr && report->width == 640 && report->height == 360 && report->bufferCount == 2 &&
                    report->currentBackBufferIndex < 2 && report->descriptorShapeIsValid &&
                    report->backBuffersAreAvailable && report->altEnterWasDisabled &&
                    report->isVsyncEnabled == isVsyncEnabled &&
                    report->isTearingEnabled == (!isVsyncEnabled && report->isTearingSupported) &&
                    report->infoQueueErrorCount == 0;
            failureCode = valid ? 0 : 5;
        }
    }
    else if (mode == "TearingMatrix")
    {
        valid = cue::verify_d3d12_swap_chain_tearing_matrix_for_probe(nativeWindow, 640, 360, assertContext);
        failureCode = valid ? 0 : 6;
    }
    else if (mode == "CreationFaults")
    {
        valid = cue::verify_d3d12_swap_chain_faults_for_probe(nativeWindow, 640, 360, assertContext);
        failureCode = valid ? 0 : 7;
    }
    else if (mode == "LogFailure")
    {
        std::vector<std::unique_ptr<cue::LogSink>> failingSinks;
        failingSinks.push_back(std::make_unique<FailingLogSink>());
        cue::Logger failingLogger(fatalHandler, std::move(failingSinks));
        cue::AssertContext failingAssertContext(failingLogger, fatalHandler);
        valid = cue::verify_d3d12_swap_chain_log_failure_for_probe(nativeWindow, 640, 360, assertContext,
                                                                   failingAssertContext);
        failureCode = valid ? 0 : 12;
    }
    else if (mode == "Validation")
    {
        valid = cue::verify_d3d12_swap_chain_validation_for_probe(nativeWindow, 640, 360, assertContext);
        failureCode = valid ? 0 : 8;
    }
    else if (mode == "ProductionOwnership")
    {
        valid = run_production_ownership(*window, assertContext);
        failureCode = valid ? 0 : 9;
    }
    else if (mode == "ResizeLifecycle")
    {
        valid = run_resize_lifecycle(*window, *processSinkView, assertContext);
        failureCode = valid ? 0 : 19;
    }
    else if (mode == "ResizeFailure")
    {
        valid = cue::verify_d3d12_swap_chain_resize_failure_for_probe(nativeWindow, 640, 360, assertContext);
        failureCode = valid ? 0 : 20;
    }
    else if (mode == "RtvRebuildFailure")
    {
        valid = cue::verify_d3d12_rtv_rebuild_failure_for_probe(nativeWindow, 640, 360, assertContext);
        failureCode = valid ? 0 : 21;
    }
    else if (mode == "TerminalResizeRejection")
    {
        valid = cue::verify_d3d12_terminal_resize_rejection_for_probe(nativeWindow, 640, 360, assertContext);
        failureCode = valid ? 0 : 25;
    }
    else if (mode == "ResizeUnavailableRetention")
    {
        valid = cue::verify_d3d12_resize_unavailable_retention_for_probe(nativeWindow, 640, 360, assertContext);
        failureCode = valid ? 0 : 26;
    }
    else if (mode == "DeviceRemovalLifecycle")
    {
        failureCode = run_device_removal_lifecycle(*window, assertContext);
        valid = failureCode == 0 || failureCode == 77;
    }
    else if (mode == "DeviceRemovalResize")
    {
        failureCode = run_device_removal_resize(*window, assertContext);
        valid = failureCode == 0 || failureCode == 77;
    }
    else if (mode == "DeviceRemovalDredFailure")
    {
        failureCode = run_device_removal_dred_failure(*window, *processSinkView, assertContext);
        valid = failureCode == 0 || failureCode == 77;
    }

    cue::Result<void> destroyResult = window->destroy();

    if (!destroyResult)
    {
        return 11;
    }

    window.reset();
    windowSystem.reset();
    return failureCode;
}
