#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Fatal.h>
#include <Cue/Foundation/Log.h>
#include <Cue/Platform/WindowSystem.h>
#include <Cue/Platform/Windows/WindowsPlatform.h>
#include <Cue/RHI/D3D12/D3d12Backend.h>
#include <Cue/RHI/D3D12/Windows/D3d12WindowsPresentation.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifndef CUE_RUNTIME_GRAPHICS_DIAGNOSTICS_DEFAULT
#error CUE_RUNTIME_GRAPHICS_DIAGNOSTICS_DEFAULT must be provided by the CueRuntimeHost CMake target
#endif

namespace
{
constexpr int k_invalidArguments = 64;
constexpr int k_systemCreationFailed = 1;
constexpr int k_windowCreationFailed = 2;
constexpr int k_windowShowFailed = 3;
constexpr int k_messagePumpFailed = 4;
constexpr int k_windowDestroyFailed = 5;
constexpr int k_graphicsBackendCreationFailed = 6;
constexpr int k_graphicsBackendShutdownFailed = 7;
constexpr int k_graphicsLogFailed = 8;
constexpr int k_presentationCreationFailed = 9;
constexpr int k_presentationShutdownFailed = 10;
constexpr int k_presentationFrameFailed = 11;

struct RuntimeOptions final
{
    std::string title = "CueEngine Runtime Host";
    cue::WindowSize clientSize = {1280, 720};
    bool isSmokeTest = false;
    bool isGraphicsSmoke = false;
    bool isPresentationSmoke = false;
    bool isRenderSmoke = false;
    cue::D3d12AdapterPolicy graphicsAdapterPolicy = cue::D3d12AdapterPolicy::HighPerformanceHardware;
};

[[nodiscard]] bool parse_size(std::wstring_view a_text, std::uint32_t &a_value) noexcept
{
    if (a_text.empty())
    {
        return false;
    }

    std::uint32_t value = 0;

    for (wchar_t character : a_text)
    {
        if (character < L'0' || character > L'9')
        {
            return false;
        }

        std::uint32_t digit = static_cast<std::uint32_t>(character - L'0');

        if (value > (std::numeric_limits<std::uint32_t>::max() - digit) / 10)
        {
            return false;
        }

        value = value * 10 + digit;
    }

    if (value == 0)
    {
        return false;
    }

    a_value = value;
    return true;
}

[[nodiscard]] cue::Result<bool> parse_options(int a_argumentCount, wchar_t **a_arguments, RuntimeOptions &a_options,
                                              const cue::AssertContext &a_assertContext) noexcept
{
    for (int index = 1; index < a_argumentCount; ++index)
    {
        std::wstring_view argument = a_arguments[index];

        if (argument == L"--smoke-test")
        {
            a_options.isSmokeTest = true;
            continue;
        }

        if (argument == L"--graphics-smoke" || argument == L"--presentation-smoke" ||
            argument == L"--render-smoke")
        {
            if (index + 1 >= a_argumentCount)
            {
                return cue::Result<bool>::success(false);
            }

            std::wstring_view value = a_arguments[++index];

            if (value == L"hardware")
            {
                a_options.graphicsAdapterPolicy = cue::D3d12AdapterPolicy::HighPerformanceHardware;
            }
            else if (value == L"warp")
            {
                a_options.graphicsAdapterPolicy = cue::D3d12AdapterPolicy::Warp;
            }
            else
            {
                return cue::Result<bool>::success(false);
            }

            if (argument == L"--graphics-smoke")
            {
                a_options.isGraphicsSmoke = true;
            }
            else if (argument == L"--presentation-smoke")
            {
                a_options.isPresentationSmoke = true;
            }
            else
            {
                a_options.isRenderSmoke = true;
            }
            continue;
        }

        if (index + 1 >= a_argumentCount)
        {
            return cue::Result<bool>::success(false);
        }

        std::wstring_view value = a_arguments[++index];

        if (argument == L"--title")
        {
            cue::Result<std::string> titleResult = cue::convert_windows_argument_to_utf8(value, a_assertContext);

            if (!titleResult)
            {
                return cue::Result<bool>::failure(std::move(*titleResult.try_error()));
            }

            a_options.title = std::move(*titleResult.try_value());
        }
        else if (argument == L"--width")
        {
            if (!parse_size(value, a_options.clientSize.width))
            {
                return cue::Result<bool>::success(false);
            }
        }
        else if (argument == L"--height")
        {
            if (!parse_size(value, a_options.clientSize.height))
            {
                return cue::Result<bool>::success(false);
            }
        }
        else
        {
            return cue::Result<bool>::success(false);
        }
    }

    const int modeCount = static_cast<int>(a_options.isSmokeTest) + static_cast<int>(a_options.isGraphicsSmoke) +
                          static_cast<int>(a_options.isPresentationSmoke) + static_cast<int>(a_options.isRenderSmoke);
    return cue::Result<bool>::success(modeCount <= 1);
}

void print_usage() noexcept
{
    std::fputws(L"Usage: CueRuntimeHost [--smoke-test | --graphics-smoke <hardware|warp> | "
                L"--presentation-smoke <hardware|warp> | --render-smoke <hardware|warp>] "
                L"[--title <title>] [--width <pixels>] [--height <pixels>]\n",
                stderr);
}

[[nodiscard]] int report_error(cue::Logger &a_logger, std::string_view a_message, cue::Error &&a_error,
                               int a_exitCode) noexcept;

[[nodiscard]] cue::D3d12BackendDescriptor make_backend_descriptor(const RuntimeOptions &a_options) noexcept
{
    constexpr bool enableDiagnostics = CUE_RUNTIME_GRAPHICS_DIAGNOSTICS_DEFAULT != 0;
    return {
        a_options.graphicsAdapterPolicy,
        enableDiagnostics ? cue::D3d12ValidationMode::Standard : cue::D3d12ValidationMode::Disabled,
        enableDiagnostics,
        5000,
    };
}

void add_secondary_runtime_error(cue::Error &a_primaryError, const cue::Error &a_secondaryError,
                                 std::string_view a_context, const cue::AssertContext &a_assertContext) noexcept
{
    try
    {
        a_primaryError.add_context(a_assertContext.fatal_handler(), a_context);
        a_primaryError.add_context(a_assertContext.fatal_handler(), a_secondaryError.summary());
        std::string codeContext = "Secondary Runtime Error Code=";
        codeContext.append(a_secondaryError.code().domain());
        codeContext.push_back('/');
        codeContext.append(std::to_string(a_secondaryError.code().value()));
        a_primaryError.add_context(a_assertContext.fatal_handler(), codeContext);

        const cue::NativeError *nativeError = a_secondaryError.try_native_error();

        if (nativeError != nullptr)
        {
            std::string nativeContext = "Secondary Runtime Error NativeError=";
            nativeContext.append(nativeError->domain());
            nativeContext.push_back('/');
            nativeContext.append(std::to_string(nativeError->value()));
            a_primaryError.add_context(a_assertContext.fatal_handler(), nativeContext);
        }

        for (const cue::ErrorContext &context : a_secondaryError.contexts())
        {
            a_primaryError.add_context(a_assertContext.fatal_handler(), context.message());
        }

        for (const cue::ErrorCause &cause : a_secondaryError.causes())
        {
            a_primaryError.add_context(a_assertContext.fatal_handler(), cause.summary());
            std::string causeCodeContext = "Secondary Runtime Error Cause Code=";
            causeCodeContext.append(cause.code().domain());
            causeCodeContext.push_back('/');
            causeCodeContext.append(std::to_string(cause.code().value()));
            a_primaryError.add_context(a_assertContext.fatal_handler(), causeCodeContext);

            const cue::NativeError *causeNativeError = cause.try_native_error();

            if (causeNativeError != nullptr)
            {
                std::string causeNativeContext = "Secondary Runtime Error Cause NativeError=";
                causeNativeContext.append(causeNativeError->domain());
                causeNativeContext.push_back('/');
                causeNativeContext.append(std::to_string(causeNativeError->value()));
                a_primaryError.add_context(a_assertContext.fatal_handler(), causeNativeContext);
            }

            for (const cue::ErrorContext &context : cause.contexts())
            {
                a_primaryError.add_context(a_assertContext.fatal_handler(), context.message());
            }
        }
    }
    catch (...)
    {
        a_assertContext.fatal_handler().terminate("Runtime Host Error context allocation failed");
    }
}

[[nodiscard]] int run_graphics_smoke(const RuntimeOptions &a_options, cue::Logger &a_logger,
                                     cue::AssertContext &a_assertContext)
{
    cue::D3d12BackendDescriptor descriptor = make_backend_descriptor(a_options);
    cue::Result<std::unique_ptr<cue::D3d12Backend>> backendResult =
        cue::create_d3d12_backend(descriptor, a_assertContext);

    if (!backendResult)
    {
        return report_error(a_logger, "Runtime Host failed to create D3D12 Backend",
                            std::move(*backendResult.try_error()), k_graphicsBackendCreationFailed);
    }

    std::unique_ptr<cue::D3d12Backend> backend = std::move(*backendResult.try_value());
    const cue::CapabilityReport &capabilities = backend->capabilities();
    std::string capabilityMessage =
        "D3D12 Device Smoke ready: Adapter=" + capabilities.adapterName +
        ", VendorId=" + std::to_string(capabilities.vendorId) + ", DeviceId=" + std::to_string(capabilities.deviceId) +
        ", DedicatedVideoMemoryBytes=" + std::to_string(capabilities.dedicatedVideoMemoryBytes) +
        ", UMA=" + (capabilities.isUma ? "true" : "false");
    cue::LogResult capabilityLogResult = a_logger.log(cue::LogLevel::Info, capabilityMessage);
    cue::Result<void> shutdownResult = backend->shutdown();

    if (!shutdownResult)
    {
        if (backend->state() == cue::GraphicsBackendState::Unavailable)
        {
            cue::report_fatal(a_logger, a_assertContext.fatal_handler(),
                              "Runtime Host could not prove safe D3D12 Backend shutdown",
                              std::move(*shutdownResult.try_error()));
        }

        backend.reset();
        return report_error(a_logger, "Runtime Host failed to shutdown D3D12 Backend",
                            std::move(*shutdownResult.try_error()), k_graphicsBackendShutdownFailed);
    }

    backend.reset();

    if (capabilityLogResult != cue::LogResult::Success)
    {
        return k_graphicsLogFailed;
    }

    cue::LogResult shutdownLogResult = a_logger.log(cue::LogLevel::Info, "D3D12 Device Smoke shutdown completed");
    cue::LogResult flushResult = a_logger.flush();
    return shutdownLogResult == cue::LogResult::Success && flushResult == cue::LogResult::Success ? 0
                                                                                                  : k_graphicsLogFailed;
}

[[nodiscard]] int run_presentation_smoke(const RuntimeOptions &a_options, cue::Window &a_window, cue::Logger &a_logger,
                                         cue::AssertContext &a_assertContext)
{
    cue::D3d12BackendDescriptor backendDescriptor = make_backend_descriptor(a_options);
    cue::Result<std::unique_ptr<cue::D3d12Backend>> backendResult =
        cue::create_d3d12_backend(backendDescriptor, a_assertContext);

    if (!backendResult)
    {
        return report_error(a_logger, "Runtime Host failed to create D3D12 Backend",
                            std::move(*backendResult.try_error()), k_graphicsBackendCreationFailed);
    }

    std::unique_ptr<cue::D3d12Backend> backend = std::move(*backendResult.try_value());
    cue::PresentationDescriptor presentationDescriptor = {true};
    cue::Result<std::unique_ptr<cue::PresentationContext>> presentationResult =
        cue::create_d3d12_windows_presentation(*backend, a_window, presentationDescriptor);

    if (!presentationResult)
    {
        cue::Error presentationError = std::move(*presentationResult.try_error());
        cue::Result<void> backendShutdownResult = backend->shutdown();

        if (!backendShutdownResult)
        {
            cue::report_fatal(a_logger, a_assertContext.fatal_handler(),
                              "Runtime Host could not cleanup Backend after Presentation creation failure",
                              std::move(*backendShutdownResult.try_error()));
        }

        backend.reset();
        return report_error(a_logger, "Runtime Host failed to create D3D12 Presentation", std::move(presentationError),
                            k_presentationCreationFailed);
    }

    std::unique_ptr<cue::PresentationContext> presentation = std::move(*presentationResult.try_value());
    std::string presentationMessage =
        "D3D12 Presentation Smoke ready: Width=" + std::to_string(presentation->width()) +
        ", Height=" + std::to_string(presentation->height()) +
        ", BufferCount=" + std::to_string(presentation->buffer_count()) +
        ", CurrentBackBufferIndex=" + std::to_string(presentation->current_back_buffer_index()) +
        ", VSync=" + (presentation->is_vsync_enabled() ? "true" : "false") +
        ", TearingSupported=" + (presentation->is_tearing_supported() ? "true" : "false") +
        ", TearingEnabled=" + (presentation->is_tearing_enabled() ? "true" : "false");
    cue::LogResult presentationLogResult = a_logger.log(cue::LogLevel::Info, presentationMessage);
    cue::Result<void> presentationShutdownResult = presentation->shutdown();

    if (!presentationShutdownResult)
    {
        cue::report_fatal(a_logger, a_assertContext.fatal_handler(),
                          "Runtime Host could not prove safe D3D12 Presentation shutdown",
                          std::move(*presentationShutdownResult.try_error()));
    }

    presentation.reset();
    cue::Result<void> backendShutdownResult = backend->shutdown();

    if (!backendShutdownResult)
    {
        if (backend->state() == cue::GraphicsBackendState::Unavailable)
        {
            cue::report_fatal(a_logger, a_assertContext.fatal_handler(),
                              "Runtime Host could not prove safe D3D12 Backend shutdown",
                              std::move(*backendShutdownResult.try_error()));
        }

        backend.reset();
        return report_error(a_logger, "Runtime Host failed to shutdown D3D12 Backend",
                            std::move(*backendShutdownResult.try_error()), k_graphicsBackendShutdownFailed);
    }

    backend.reset();

    if (presentationLogResult != cue::LogResult::Success)
    {
        return k_graphicsLogFailed;
    }

    cue::LogResult shutdownLogResult = a_logger.log(cue::LogLevel::Info, "D3D12 Presentation Smoke shutdown completed");
    cue::LogResult flushResult = a_logger.flush();
    return shutdownLogResult == cue::LogResult::Success && flushResult == cue::LogResult::Success
               ? 0
               : k_presentationShutdownFailed;
}

[[nodiscard]] int run_render_loop(const RuntimeOptions &a_options, cue::WindowSystem &a_windowSystem,
                                  cue::Window &a_window, cue::Logger &a_logger,
                                  cue::AssertContext &a_assertContext)
{
    constexpr std::uint64_t renderSmokeFrameCount = 300;
    constexpr std::array<float, 4> clearColor = {0.06F, 0.18F, 0.32F, 1.0F};
    cue::D3d12BackendDescriptor backendDescriptor = make_backend_descriptor(a_options);
    cue::Result<std::unique_ptr<cue::D3d12Backend>> backendResult =
        cue::create_d3d12_backend(backendDescriptor, a_assertContext);

    if (!backendResult)
    {
        return report_error(a_logger, "Runtime Host failed to create D3D12 Backend",
                            std::move(*backendResult.try_error()), k_graphicsBackendCreationFailed);
    }

    std::unique_ptr<cue::D3d12Backend> backend = std::move(*backendResult.try_value());
    cue::PresentationDescriptor presentationDescriptor = {true};
    cue::Result<std::unique_ptr<cue::PresentationContext>> presentationResult =
        cue::create_d3d12_windows_presentation(*backend, a_window, presentationDescriptor);

    if (!presentationResult)
    {
        cue::Error presentationError = std::move(*presentationResult.try_error());
        cue::Result<void> backendShutdownResult = backend->shutdown();

        if (!backendShutdownResult)
        {
            cue::report_fatal(a_logger, a_assertContext.fatal_handler(),
                              "Runtime Host could not cleanup Backend after Presentation creation failure",
                              std::move(*backendShutdownResult.try_error()));
        }

        backend.reset();
        return report_error(a_logger, "Runtime Host failed to create D3D12 Presentation",
                            std::move(presentationError), k_presentationCreationFailed);
    }

    std::unique_ptr<cue::PresentationContext> presentation = std::move(*presentationResult.try_value());
    std::string readyMessage =
        "D3D12 Render Loop ready: Width=" + std::to_string(presentation->width()) +
        ", Height=" + std::to_string(presentation->height()) +
        ", BufferCount=" + std::to_string(presentation->buffer_count()) +
        ", VSync=" + (presentation->is_vsync_enabled() ? "true" : "false");
    cue::LogResult readyLogResult = a_logger.log(cue::LogLevel::Info, readyMessage);
    std::optional<cue::Error> frameError;
    std::string_view loopErrorMessage = "Runtime Host rendering Frame failed";
    int loopErrorExitCode = k_presentationFrameFailed;
    std::uint64_t frameCount = 0;
    std::uint64_t occludedFrameCount = 0;
    bool isShutdownRequested = false;

    while (!isShutdownRequested)
    {
        cue::Result<cue::PumpStatus> pumpResult = a_windowSystem.pump_events();

        if (!pumpResult)
        {
            frameError.emplace(std::move(*pumpResult.try_error()));
            loopErrorMessage = "Runtime Host Message Pump failed";
            loopErrorExitCode = k_messagePumpFailed;
            break;
        }

        if (*pumpResult.try_value() == cue::PumpStatus::QuitRequested)
        {
            isShutdownRequested = true;
        }

        cue::WindowEvent event = {};

        while (a_window.try_pop_event(event))
        {
            if (event.type == cue::WindowEventType::CloseRequested || event.type == cue::WindowEventType::Destroyed)
            {
                isShutdownRequested = true;
            }
        }

        if (isShutdownRequested)
        {
            break;
        }

        cue::PresentationFrameDescriptor frameDescriptor = {clearColor};
        cue::Result<cue::PresentationFrameStatus> frameResult = presentation->present_frame(frameDescriptor);

        if (!frameResult)
        {
            frameError.emplace(std::move(*frameResult.try_error()));
            break;
        }

        ++frameCount;

        if (*frameResult.try_value() == cue::PresentationFrameStatus::Occluded)
        {
            ++occludedFrameCount;
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        if (a_options.isRenderSmoke && frameCount >= renderSmokeFrameCount)
        {
            isShutdownRequested = true;
        }
    }

    const std::uint32_t finalBackBufferIndex = presentation->current_back_buffer_index();
    cue::Result<void> presentationShutdownResult = presentation->shutdown();

    if (!presentationShutdownResult && presentation->state() == cue::PresentationContextState::Unavailable)
    {
        cue::report_fatal(a_logger, a_assertContext.fatal_handler(),
                          "Runtime Host could not prove safe D3D12 Presentation shutdown",
                          std::move(*presentationShutdownResult.try_error()));
    }

    std::optional<cue::Error> presentationShutdownError;

    if (!presentationShutdownResult)
    {
        presentationShutdownError.emplace(std::move(*presentationShutdownResult.try_error()));
    }

    presentation.reset();
    cue::Result<void> backendShutdownResult = backend->shutdown();

    if (!backendShutdownResult && backend->state() == cue::GraphicsBackendState::Unavailable)
    {
        cue::report_fatal(a_logger, a_assertContext.fatal_handler(),
                          "Runtime Host could not prove safe D3D12 Backend shutdown",
                          std::move(*backendShutdownResult.try_error()));
    }

    std::optional<cue::Error> backendShutdownError;

    if (!backendShutdownResult)
    {
        backendShutdownError.emplace(std::move(*backendShutdownResult.try_error()));
    }

    backend.reset();

    if (frameError)
    {
        if (presentationShutdownError)
        {
            add_secondary_runtime_error(*frameError, *presentationShutdownError,
                                        "D3D12 Presentation shutdown also failed after rendering Error",
                                        a_assertContext);
        }

        if (backendShutdownError)
        {
            add_secondary_runtime_error(*frameError, *backendShutdownError,
                                        "D3D12 Backend shutdown also failed after rendering Error",
                                        a_assertContext);
        }

        return report_error(a_logger, loopErrorMessage, std::move(*frameError), loopErrorExitCode);
    }

    if (presentationShutdownError)
    {
        if (backendShutdownError)
        {
            add_secondary_runtime_error(*presentationShutdownError, *backendShutdownError,
                                        "D3D12 Backend shutdown also failed after Presentation shutdown Error",
                                        a_assertContext);
        }

        return report_error(a_logger, "Runtime Host failed to shutdown D3D12 Presentation",
                            std::move(*presentationShutdownError), k_presentationShutdownFailed);
    }

    if (backendShutdownError)
    {
        return report_error(a_logger, "Runtime Host failed to shutdown D3D12 Backend",
                            std::move(*backendShutdownError), k_graphicsBackendShutdownFailed);
    }

    std::string completionMessage =
        "D3D12 Render Loop completed: FrameCount=" + std::to_string(frameCount) +
        ", OccludedFrameCount=" + std::to_string(occludedFrameCount) +
        ", FinalBackBufferIndex=" + std::to_string(finalBackBufferIndex);
    cue::LogResult completionLogResult = a_logger.log(cue::LogLevel::Info, completionMessage);
    cue::LogResult shutdownLogResult = a_logger.log(cue::LogLevel::Info, "D3D12 Render Loop shutdown completed");
    cue::LogResult flushResult = a_logger.flush();
    return readyLogResult == cue::LogResult::Success && completionLogResult == cue::LogResult::Success &&
                   shutdownLogResult == cue::LogResult::Success && flushResult == cue::LogResult::Success
               ? 0
               : k_graphicsLogFailed;
}

[[nodiscard]] int report_error(cue::Logger &a_logger, std::string_view a_message, cue::Error &&a_error,
                               int a_exitCode) noexcept
{
    static_cast<void>(a_logger.log(cue::LogLevel::Error, a_message, std::move(a_error)));
    static_cast<void>(a_logger.flush());
    return a_exitCode;
}

[[nodiscard]] int run(const RuntimeOptions &a_options, cue::Logger &a_logger, cue::AssertContext &a_assertContext)
{
    static_cast<void>(a_logger.log(cue::LogLevel::Info, "Runtime Host initialization started"));

    if (a_options.isGraphicsSmoke)
    {
        return run_graphics_smoke(a_options, a_logger, a_assertContext);
    }

    cue::Result<std::unique_ptr<cue::WindowSystem>> systemResult = cue::create_windows_window_system(a_assertContext);

    if (!systemResult)
    {
        return report_error(a_logger, "Runtime Host failed to create Window System",
                            std::move(*systemResult.try_error()), k_systemCreationFailed);
    }

    std::unique_ptr<cue::WindowSystem> windowSystem = std::move(*systemResult.try_value());
    cue::WindowDescriptor descriptor = {a_options.title, a_options.clientSize};
    cue::Result<std::unique_ptr<cue::Window>> windowResult = windowSystem->create_window(descriptor);

    if (!windowResult)
    {
        return report_error(a_logger, "Runtime Host failed to create Window", std::move(*windowResult.try_error()),
                            k_windowCreationFailed);
    }

    std::unique_ptr<cue::Window> window = std::move(*windowResult.try_value());

    if (a_options.isPresentationSmoke)
    {
        const int presentationResult = run_presentation_smoke(a_options, *window, a_logger, a_assertContext);
        cue::Result<void> destroyResult = window->destroy();

        if (!destroyResult)
        {
            return report_error(a_logger, "Runtime Host failed to destroy Window",
                                std::move(*destroyResult.try_error()), k_windowDestroyFailed);
        }

        window.reset();
        windowSystem.reset();
        return presentationResult;
    }

    cue::Result<void> showResult = window->show();

    if (!showResult)
    {
        return report_error(a_logger, "Runtime Host failed to show Window", std::move(*showResult.try_error()),
                            k_windowShowFailed);
    }

    if (!a_options.isSmokeTest)
    {
        const int renderResult = run_render_loop(a_options, *windowSystem, *window, a_logger, a_assertContext);
        cue::Result<void> destroyResult = window->destroy();

        if (!destroyResult)
        {
            return report_error(a_logger, "Runtime Host failed to destroy Window",
                                std::move(*destroyResult.try_error()), k_windowDestroyFailed);
        }

        window.reset();
        windowSystem.reset();
        return renderResult;
    }

    static_cast<void>(a_logger.log(cue::LogLevel::Info, "Runtime Host Main Loop started"));
    bool isShutdownRequested = a_options.isSmokeTest;

    while (true)
    {
        cue::Result<cue::PumpStatus> pumpResult = windowSystem->pump_events();

        if (!pumpResult)
        {
            return report_error(a_logger, "Runtime Host Message Pump failed", std::move(*pumpResult.try_error()),
                                k_messagePumpFailed);
        }

        bool isQuitRequested = *pumpResult.try_value() == cue::PumpStatus::QuitRequested;
        bool hasEvent = false;
        cue::WindowEvent event = {};

        while (window->try_pop_event(event))
        {
            hasEvent = true;

            if (event.type == cue::WindowEventType::CloseRequested || event.type == cue::WindowEventType::Destroyed)
            {
                isShutdownRequested = true;
            }
        }

        if (isQuitRequested)
        {
            isShutdownRequested = true;
        }

        bool didDestroy = false;

        if (isShutdownRequested && window->state() != cue::WindowState::Destroyed)
        {
            cue::Result<void> destroyResult = window->destroy();

            if (!destroyResult)
            {
                return report_error(a_logger, "Runtime Host failed to destroy Window",
                                    std::move(*destroyResult.try_error()), k_windowDestroyFailed);
            }

            didDestroy = true;
        }

        if (isQuitRequested && window->state() == cue::WindowState::Destroyed && !didDestroy)
        {
            break;
        }

        if (!hasEvent && !isShutdownRequested)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    window.reset();
    windowSystem.reset();
    static_cast<void>(a_logger.log(cue::LogLevel::Info, "Runtime Host shutdown completed"));
    static_cast<void>(a_logger.flush());
    return 0;
}
} // namespace

int wmain(int a_argumentCount, wchar_t **a_arguments)
{
    cue::AbortFatalHandler fatalHandler;

    try
    {
        std::vector<std::unique_ptr<cue::LogSink>> sinks;
        sinks.push_back(std::make_unique<cue::ConsoleLogSink>());
        cue::Logger logger(fatalHandler, std::move(sinks));
        cue::AssertContext assertContext(logger, fatalHandler);
        RuntimeOptions options;
        cue::Result<bool> optionsResult = parse_options(a_argumentCount, a_arguments, options, assertContext);

        if (!optionsResult)
        {
            return report_error(logger, "Runtime Host failed to convert command line",
                                std::move(*optionsResult.try_error()), k_invalidArguments);
        }

        if (!*optionsResult.try_value())
        {
            print_usage();
            return k_invalidArguments;
        }

        return run(options, logger, assertContext);
    }
    catch (...)
    {
        fatalHandler.terminate("Runtime Host allocation failed");
    }
}
