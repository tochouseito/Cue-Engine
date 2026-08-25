#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Error.h>
#include <Cue/Foundation/Fatal.h>
#include <Cue/Foundation/Log.h>
#include <Cue/Platform/WindowSystem.h>
#include <Cue/Platform/Windows/WindowsPlatform.h>
#if defined(CUE_RUNTIME_RESIZE_SMOKE_SUPPORT) && CUE_RUNTIME_RESIZE_SMOKE_SUPPORT
#include <Cue/Platform/Windows/TestSupport/WindowsWindowLifecycleProbe.h>
#endif
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

// Runtime Host は Platform と RHI を組み立て、起動から安全な終了までの Runtime 実行順を所有する
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
constexpr int k_presentationResizeFailed = 12;
#if defined(CUE_RUNTIME_RESIZE_SMOKE_SUPPORT) && CUE_RUNTIME_RESIZE_SMOKE_SUPPORT
constexpr int k_resizeSmokeFailed = 13;
constexpr std::uint32_t k_resizeSmokeCycleCount = 50;
constexpr std::uint32_t k_resizeSmokeResizeActionCount = k_resizeSmokeCycleCount * 3;
constexpr std::uint32_t k_resizeSmokeActionCount = k_resizeSmokeResizeActionCount + 1;
#endif

struct RuntimeOptions final
{
    std::string title = "CueEngine Runtime Host";
    cue::WindowSize clientSize = {1280, 720};
    bool isSmokeTest = false;
    bool isGraphicsSmoke = false;
    bool isPresentationSmoke = false;
    bool isRenderSmoke = false;
#if defined(CUE_RUNTIME_RESIZE_SMOKE_SUPPORT) && CUE_RUNTIME_RESIZE_SMOKE_SUPPORT
    bool isResizeSmoke = false;
#endif
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

        bool isGraphicsModeArgument = argument == L"--graphics-smoke" || argument == L"--presentation-smoke" ||
                                      argument == L"--render-smoke";
#if defined(CUE_RUNTIME_RESIZE_SMOKE_SUPPORT) && CUE_RUNTIME_RESIZE_SMOKE_SUPPORT
        isGraphicsModeArgument = isGraphicsModeArgument || argument == L"--resize-smoke";
#endif

        if (isGraphicsModeArgument)
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
            else if (argument == L"--render-smoke")
            {
                a_options.isRenderSmoke = true;
            }
#if defined(CUE_RUNTIME_RESIZE_SMOKE_SUPPORT) && CUE_RUNTIME_RESIZE_SMOKE_SUPPORT
            else
            {
                a_options.isResizeSmoke = true;
            }
#endif
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

    int modeCount = static_cast<int>(a_options.isSmokeTest) + static_cast<int>(a_options.isGraphicsSmoke) +
                    static_cast<int>(a_options.isPresentationSmoke) + static_cast<int>(a_options.isRenderSmoke);
#if defined(CUE_RUNTIME_RESIZE_SMOKE_SUPPORT) && CUE_RUNTIME_RESIZE_SMOKE_SUPPORT
    modeCount += static_cast<int>(a_options.isResizeSmoke);
#endif
    return cue::Result<bool>::success(modeCount <= 1);
}

void print_usage() noexcept
{
#if defined(CUE_RUNTIME_RESIZE_SMOKE_SUPPORT) && CUE_RUNTIME_RESIZE_SMOKE_SUPPORT
    std::fputws(L"Usage: CueRuntimeHost [--smoke-test | --graphics-smoke <hardware|warp> | "
                L"--presentation-smoke <hardware|warp> | --render-smoke <hardware|warp> | "
                L"--resize-smoke <hardware|warp>] "
                L"[--title <title>] [--width <pixels>] [--height <pixels>]\n",
                stderr);
#else
    std::fputws(L"Usage: CueRuntimeHost [--smoke-test | --graphics-smoke <hardware|warp> | "
                L"--presentation-smoke <hardware|warp> | --render-smoke <hardware|warp>] "
                L"[--title <title>] [--width <pixels>] [--height <pixels>]\n",
                stderr);
#endif
}

[[nodiscard]] int report_error(cue::Logger &a_logger, std::string_view a_message, cue::Error &&a_error,
                               int a_exitCode) noexcept;

[[nodiscard]] cue::D3d12BackendDescriptor make_backend_descriptor(const RuntimeOptions &a_options) noexcept
{
    // 同じ Build の診断条件を再現できるよう、Graphics 診断の既定値は Target 定義から一元的に決める
    constexpr bool enableDiagnostics = CUE_RUNTIME_GRAPHICS_DIAGNOSTICS_DEFAULT != 0;
    return {
        a_options.graphicsAdapterPolicy,
        enableDiagnostics ? cue::D3d12ValidationMode::Standard : cue::D3d12ValidationMode::Disabled,
        enableDiagnostics,
        5000,
    };
}

#if defined(CUE_RUNTIME_RESIZE_SMOKE_SUPPORT) && CUE_RUNTIME_RESIZE_SMOKE_SUPPORT
// === Resize Smoke Test Probe ===
[[nodiscard]] cue::Error make_runtime_error(const cue::AssertContext &a_assertContext,
                                            std::string_view a_summary) noexcept
{
    cue::ErrorCode code = cue::ErrorCode::create(a_assertContext.fatal_handler(), "Cue.RuntimeHost",
                                                 k_resizeSmokeFailed);
    return cue::Error::create(a_assertContext.fatal_handler(), std::move(code), a_summary);
}

[[nodiscard]] cue::Result<void> issue_resize_smoke_action(cue::Window &a_window, std::uint32_t a_actionIndex,
                                                          const cue::AssertContext &a_assertContext) noexcept
{
    if (a_actionIndex == k_resizeSmokeResizeActionCount)
    {
        return cue::issue_windows_window_lifecycle_probe_action(
            a_window, cue::WindowsWindowLifecycleProbeAction::ResizeThenClose, {832, 468}, {768, 432},
            a_assertContext);
    }

    const std::uint32_t phase = a_actionIndex % 3;

    if (phase == 0)
    {
        const std::uint32_t cycleIndex = a_actionIndex / 3;
        const cue::WindowSize finalSize = cycleIndex % 2 == 0 ? cue::WindowSize{768, 432}
                                                              : cue::WindowSize{704, 396};
        return cue::issue_windows_window_lifecycle_probe_action(
            a_window, cue::WindowsWindowLifecycleProbeAction::Resize, {832, 468}, finalSize, a_assertContext);
    }

    const cue::WindowsWindowLifecycleProbeAction action =
        phase == 1 ? cue::WindowsWindowLifecycleProbeAction::Minimize
                   : cue::WindowsWindowLifecycleProbeAction::Restore;
    return cue::issue_windows_window_lifecycle_probe_action(a_window, action, {}, {}, a_assertContext);
}
// === Resize Smoke Test Probe End ===
#endif

void add_secondary_runtime_error(cue::Error &a_primaryError, const cue::Error &a_secondaryError,
                                 std::string_view a_context, const cue::AssertContext &a_assertContext) noexcept
{
    // 最初に処理を失敗させた Error を主因として保ち、後始末の失敗も同じ診断から追跡できるようにする
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
    // Window を必要としない最小経路で Adapter と Device の生成、能力取得、安全な終了を検証する
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
    // Presentation が参照する Device と Window を先に存続させ、SwapChain の生成と破棄だけを分離検証する
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

    // Presentation が保持する GPU Resource を先に解放し、参照先の Backend を最後まで有効に保つ
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

    // Window を Presentation の Native Surface として使える状態で、Backend から依存順に描画資源を構築する
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
#if defined(CUE_RUNTIME_RESIZE_SMOKE_SUPPORT) && CUE_RUNTIME_RESIZE_SMOKE_SUPPORT
    std::uint64_t resizeEventCount = 0;
    std::uint64_t minimizeEventCount = 0;
    std::uint64_t restoreEventCount = 0;
    std::uint64_t resizeApplyCount = 0;
    std::uint64_t minimizedFrameSkipCount = 0;
    std::uint64_t resizeSmokePresentedFrameCount = 0;
    std::uint32_t resizeSmokeActionIndex = 0;
    bool isResizeSmokeBatchProbeIssued = false;
    bool isResizeSmokeStarted = false;
#endif
    bool isMinimized = false;
    bool isShutdownRequested = false;

    while (!isShutdownRequested)
    {
#if defined(CUE_RUNTIME_RESIZE_SMOKE_SUPPORT) && CUE_RUNTIME_RESIZE_SMOKE_SUPPORT
        // === Resize Smoke Test Probe ===
        if (a_options.isResizeSmoke && !isResizeSmokeStarted && !isResizeSmokeBatchProbeIssued)
        {
            cue::Result<void> minimizeResult = issue_resize_smoke_action(a_window, 1, a_assertContext);
            cue::Result<void> restoreResult = minimizeResult
                                                  ? issue_resize_smoke_action(a_window, 2, a_assertContext)
                                                  : cue::Result<void>::success();

            if (!minimizeResult || !restoreResult)
            {
                frameError.emplace(minimizeResult ? std::move(*restoreResult.try_error())
                                                  : std::move(*minimizeResult.try_error()));
                loopErrorMessage = "Runtime Host Resize Smoke batch probe failed";
                loopErrorExitCode = k_resizeSmokeFailed;
                break;
            }

            isResizeSmokeBatchProbeIssued = true;
        }

        if (a_options.isResizeSmoke && isResizeSmokeStarted &&
            resizeSmokeActionIndex < k_resizeSmokeActionCount)
        {
            cue::Result<void> actionResult =
                issue_resize_smoke_action(a_window, resizeSmokeActionIndex, a_assertContext);

            if (!actionResult)
            {
                frameError.emplace(std::move(*actionResult.try_error()));
                loopErrorMessage = "Runtime Host Resize Smoke Window operation failed";
                loopErrorExitCode = k_resizeSmokeFailed;
                break;
            }

            ++resizeSmokeActionIndex;
        }
        // === Resize Smoke Test Probe End ===
#endif

        // OS Event を先に全て取り込み、終了や最新の Window 状態を描画判断へ反映する
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
        // 同一 Pump 内の連続 Resize は最後の Client Size へ集約し、古い中間 Size で SwapChain を作り直さない
        std::optional<cue::WindowSize> pendingResize;

        while (a_window.try_pop_event(event))
        {
            if (event.type == cue::WindowEventType::CloseRequested || event.type == cue::WindowEventType::Destroyed)
            {
                isShutdownRequested = true;
            }
            else if (event.type == cue::WindowEventType::Resized)
            {
                pendingResize = event.clientSize;
                isMinimized = false;
#if defined(CUE_RUNTIME_RESIZE_SMOKE_SUPPORT) && CUE_RUNTIME_RESIZE_SMOKE_SUPPORT
                ++resizeEventCount;
#endif
            }
            else if (event.type == cue::WindowEventType::Minimized)
            {
                pendingResize = cue::WindowSize{0, 0};
                isMinimized = true;
#if defined(CUE_RUNTIME_RESIZE_SMOKE_SUPPORT) && CUE_RUNTIME_RESIZE_SMOKE_SUPPORT
                ++minimizeEventCount;
#endif
            }
            else if (event.type == cue::WindowEventType::Restored)
            {
                pendingResize = event.clientSize;
                isMinimized = false;
#if defined(CUE_RUNTIME_RESIZE_SMOKE_SUPPORT) && CUE_RUNTIME_RESIZE_SMOKE_SUPPORT
                ++restoreEventCount;
#endif
            }
        }

        if (isShutdownRequested)
        {
#if defined(CUE_RUNTIME_RESIZE_SMOKE_SUPPORT) && CUE_RUNTIME_RESIZE_SMOKE_SUPPORT
            if (a_options.isResizeSmoke)
            {
                const bool smokeSequenceValid =
                    resizeSmokeActionIndex == k_resizeSmokeActionCount &&
                    resizeEventCount == k_resizeSmokeCycleCount * 2 + 2 &&
                    minimizeEventCount == k_resizeSmokeCycleCount &&
                    restoreEventCount == k_resizeSmokeCycleCount &&
                    resizeApplyCount == k_resizeSmokeResizeActionCount &&
                    minimizedFrameSkipCount == k_resizeSmokeCycleCount &&
                    resizeSmokePresentedFrameCount == k_resizeSmokeCycleCount * 2 &&
                    presentation->width() == 704 && presentation->height() == 396;

                if (!smokeSequenceValid)
                {
                    frameError.emplace(make_runtime_error(
                        a_assertContext, "Runtime Host Resize Smoke observed an incomplete Window Event sequence"));
                    loopErrorMessage = "Runtime Host Resize Smoke sequence failed";
                    loopErrorExitCode = k_resizeSmokeFailed;
                }
            }
#endif

            break;
        }

        if (pendingResize)
        {
            // Event drain 後に一度だけ適用し、Window と Presentation の Size を Frame 境界で同期する
            cue::Result<void> resizeResult =
                presentation->resize(pendingResize->width, pendingResize->height);

            if (!resizeResult)
            {
                frameError.emplace(std::move(*resizeResult.try_error()));
                loopErrorMessage = "Runtime Host failed to resize D3D12 Presentation";
                loopErrorExitCode = k_presentationResizeFailed;
                break;
            }

#if defined(CUE_RUNTIME_RESIZE_SMOKE_SUPPORT) && CUE_RUNTIME_RESIZE_SMOKE_SUPPORT
            ++resizeApplyCount;
            if (a_options.isResizeSmoke && pendingResize->width != 0 && pendingResize->height != 0 &&
                (presentation->width() != pendingResize->width || presentation->height() != pendingResize->height))
            {
                frameError.emplace(make_runtime_error(
                    a_assertContext, "Runtime Host Resize Smoke did not apply the latest Window Client Size"));
                loopErrorMessage = "Runtime Host Resize Smoke size verification failed";
                loopErrorExitCode = k_resizeSmokeFailed;
                break;
            }
#endif
        }

        if (isMinimized || presentation->is_resize_pending())
        {
#if defined(CUE_RUNTIME_RESIZE_SMOKE_SUPPORT) && CUE_RUNTIME_RESIZE_SMOKE_SUPPORT
            ++minimizedFrameSkipCount;
#endif
            // 描画可能な Client Area がない間は GPU へ Frame を投入せず、待機中の Busy Loop も避ける
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        // Clear と Present を一つの Frame として繰り返し、最小 Rendering 経路が継続動作することを保証する
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

#if defined(CUE_RUNTIME_RESIZE_SMOKE_SUPPORT) && CUE_RUNTIME_RESIZE_SMOKE_SUPPORT
        if (a_options.isResizeSmoke)
        {
            if (!isResizeSmokeStarted)
            {
                const bool batchProbeValid = isResizeSmokeBatchProbeIssued && minimizeEventCount == 1 &&
                                             restoreEventCount == 1 && !isMinimized &&
                                             !presentation->is_resize_pending();

                if (!batchProbeValid)
                {
                    frameError.emplace(make_runtime_error(
                        a_assertContext, "Runtime Host Resize Smoke did not preserve the latest batched Window state"));
                    loopErrorMessage = "Runtime Host Resize Smoke batch verification failed";
                    loopErrorExitCode = k_resizeSmokeFailed;
                    break;
                }

                resizeEventCount = 0;
                minimizeEventCount = 0;
                restoreEventCount = 0;
                resizeApplyCount = 0;
                minimizedFrameSkipCount = 0;
                isResizeSmokeStarted = true;
            }
            else
            {
                ++resizeSmokePresentedFrameCount;
            }
        }
#endif

        if (a_options.isRenderSmoke && frameCount >= renderSmokeFrameCount)
        {
            isShutdownRequested = true;
        }
    }

    const std::uint32_t finalBackBufferIndex = presentation->current_back_buffer_index();

    // GPU 使用中の Presentation を先に停止し、依存先の Backend はその完了確認まで存続させる
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

    // Presentation の所有物がなくなってから Device を停止し、参照先を先に破棄する順序を防ぐ
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

    // Rendering Error を Primary とし、終了処理で増えた Error は Secondary として診断情報へ統合する
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
        // Frame が成功した場合は、依存関係上先に失敗した Presentation 終了を Primary として扱う
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

    cue::LogResult resizeSmokeLogResult = cue::LogResult::Success;

#if defined(CUE_RUNTIME_RESIZE_SMOKE_SUPPORT) && CUE_RUNTIME_RESIZE_SMOKE_SUPPORT
    if (a_options.isResizeSmoke)
    {
        std::string resizeSmokeMessage =
            "D3D12 Resize Smoke completed: ResizeEventCount=" + std::to_string(resizeEventCount) +
            ", MinimizeEventCount=" + std::to_string(minimizeEventCount) +
            ", RestoreEventCount=" + std::to_string(restoreEventCount) +
            ", ResizeApplyCount=" + std::to_string(resizeApplyCount) +
            ", MinimizedFrameSkipCount=" + std::to_string(minimizedFrameSkipCount) +
            ", PresentedFrameCount=" + std::to_string(resizeSmokePresentedFrameCount);
        resizeSmokeLogResult = a_logger.log(cue::LogLevel::Info, resizeSmokeMessage);
    }
#endif

    std::string completionMessage =
        "D3D12 Render Loop completed: FrameCount=" + std::to_string(frameCount) +
        ", OccludedFrameCount=" + std::to_string(occludedFrameCount) +
        ", FinalBackBufferIndex=" + std::to_string(finalBackBufferIndex);
    cue::LogResult completionLogResult = a_logger.log(cue::LogLevel::Info, completionMessage);
    cue::LogResult shutdownLogResult = a_logger.log(cue::LogLevel::Info, "D3D12 Render Loop shutdown completed");
    cue::LogResult flushResult = a_logger.flush();
    return readyLogResult == cue::LogResult::Success && resizeSmokeLogResult == cue::LogResult::Success &&
                   completionLogResult == cue::LogResult::Success &&
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

    // Graphics Smoke は Window 依存を除外し、Backend 単体の失敗範囲を明確にする
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

    // WindowSystem を Window より長生きさせ、Platform Event と Native Window の所有順を固定する
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
        // Presentation Smoke は表示や Main Loop を省き、Window Surface と SwapChain の境界だけを検証する
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
        // 通常実行と Rendering 系 Smoke は同じ Clear/Present 経路を通し、Smoke 固有分岐による差を抑える
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

    // Window Smoke は生成直後から終了要求を立て、実描画なしで Event drain と破棄手順を検証する
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
            // Destroy 後の Quit Event まで Pump を続け、Platform 側の Window 終了完了を確認する
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
    // FatalHandler を最外側に置き、Logger と AssertContext の構築から破棄まで異常終了先を存続させる
    cue::AbortFatalHandler fatalHandler;

    try
    {
        std::vector<std::unique_ptr<cue::LogSink>> sinks;
        sinks.push_back(std::make_unique<cue::ConsoleLogSink>());
        cue::Logger logger(fatalHandler, std::move(sinks));

        // AssertContext を Logger より後に構築し、逆順破棄でも診断先が先に失われないようにする
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
