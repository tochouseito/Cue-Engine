#include <Cue/Platform/Windows/TestSupport/WindowsWindowLifecycleProbe.h>

#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Error.h>
#include <Cue/Platform/Windows/WindowsWindowInterop.h>

#include <Windows.h>

#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>

namespace
{
constexpr std::int64_t k_windowOperationFailed = 1;

[[nodiscard]] cue::Error make_native_error(const cue::AssertContext &a_assertContext,
                                           std::string_view a_summary, DWORD a_nativeCode) noexcept
{
    cue::ErrorCode code = cue::ErrorCode::create(a_assertContext.fatal_handler(),
                                                 "Cue.Platform.Windows.TestSupport", k_windowOperationFailed);
    cue::NativeError nativeError = cue::NativeError::create(a_assertContext.fatal_handler(), "Win32", a_nativeCode);
    return cue::Error::create(a_assertContext.fatal_handler(), std::move(code), a_summary,
                              std::move(nativeError));
}

[[nodiscard]] cue::Result<void> resize_client_area(HWND a_window, cue::WindowSize a_size,
                                                   const cue::AssertContext &a_assertContext) noexcept
{
    RECT clientRectangle = {};
    RECT windowRectangle = {};

    if (GetClientRect(a_window, &clientRectangle) == FALSE || GetWindowRect(a_window, &windowRectangle) == FALSE)
    {
        return cue::Result<void>::failure(make_native_error(
            a_assertContext, "Windows Window Lifecycle Probe could not query the Window rectangle", GetLastError()));
    }

    const std::int64_t nonClientWidth =
        (static_cast<std::int64_t>(windowRectangle.right) - static_cast<std::int64_t>(windowRectangle.left)) -
        (static_cast<std::int64_t>(clientRectangle.right) - static_cast<std::int64_t>(clientRectangle.left));
    const std::int64_t nonClientHeight =
        (static_cast<std::int64_t>(windowRectangle.bottom) - static_cast<std::int64_t>(windowRectangle.top)) -
        (static_cast<std::int64_t>(clientRectangle.bottom) - static_cast<std::int64_t>(clientRectangle.top));
    const std::int64_t width = static_cast<std::int64_t>(a_size.width) + nonClientWidth;
    const std::int64_t height = static_cast<std::int64_t>(a_size.height) + nonClientHeight;

    if (width <= 0 || height <= 0 || width > std::numeric_limits<int>::max() ||
        height > std::numeric_limits<int>::max())
    {
        return cue::Result<void>::failure(make_native_error(
            a_assertContext, "Windows Window Lifecycle Probe received an unsupported Client Size",
            ERROR_INVALID_PARAMETER));
    }

    if (SetWindowPos(a_window, nullptr, 0, 0, static_cast<int>(width), static_cast<int>(height),
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) == FALSE)
    {
        return cue::Result<void>::failure(make_native_error(
            a_assertContext, "Windows Window Lifecycle Probe could not resize the Window", GetLastError()));
    }

    return cue::Result<void>::success();
}
} // namespace

namespace cue
{
Result<void> issue_windows_window_lifecycle_probe_action(
    Window &a_window, WindowsWindowLifecycleProbeAction a_action, WindowSize a_firstSize,
    WindowSize a_finalSize, const AssertContext &a_assertContext) noexcept
{
    Result<NativeWindowView> nativeViewResult = get_native_window_view(a_window, a_assertContext);

    if (!nativeViewResult)
    {
        return Result<void>::failure(std::move(*nativeViewResult.try_error()));
    }

    const NativeWindowView &nativeView = *nativeViewResult.try_value();
    HWND nativeWindow = reinterpret_cast<HWND>(const_cast<void *>(nativeView.value()));

    if (a_action == WindowsWindowLifecycleProbeAction::Minimize)
    {
        static_cast<void>(ShowWindow(nativeWindow, SW_MINIMIZE));
        return Result<void>::success();
    }

    if (a_action == WindowsWindowLifecycleProbeAction::Restore)
    {
        static_cast<void>(ShowWindow(nativeWindow, SW_RESTORE));
        return Result<void>::success();
    }

    Result<void> firstResizeResult = resize_client_area(nativeWindow, a_firstSize, a_assertContext);

    if (!firstResizeResult)
    {
        return firstResizeResult;
    }

    Result<void> finalResizeResult = resize_client_area(nativeWindow, a_finalSize, a_assertContext);

    if (!finalResizeResult)
    {
        return finalResizeResult;
    }

    if (a_action == WindowsWindowLifecycleProbeAction::ResizeThenClose)
    {
        static_cast<void>(SendMessageW(nativeWindow, WM_CLOSE, 0, 0));
    }

    return Result<void>::success();
}
} // namespace cue
