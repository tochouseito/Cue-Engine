#include <Cue/Platform/Windows/TestSupport/WindowsWindowLifecycleProbe.h>

#include "../WindowsUtilities.h"

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
using cue::windows_private::make_native_error;
using cue::windows_private::query_client_size;

constexpr std::int64_t k_windowOperationFailed = 1;
constexpr std::string_view k_errorDomain = "Cue.Platform.Windows.TestSupport";
constexpr std::string_view k_rectangleQueryFailed =
    "Windows Window Lifecycle Probe could not query the Window rectangle";

/// @brief Win32 Window Lifecycle Probe の Client Area を指定 Size へ再構築し、後続処理へ反映する
[[nodiscard]] cue::Result<void> resize_client_area(HWND a_window, cue::WindowSize a_size,
                                                   const cue::AssertContext &a_assertContext) noexcept
{
    RECT windowRectangle = {};
    cue::Result<cue::WindowSize> clientSizeResult =
        query_client_size(a_window, a_assertContext, k_errorDomain, k_windowOperationFailed, k_rectangleQueryFailed);

    if (!clientSizeResult)
    {
        return cue::Result<void>::failure(std::move(*clientSizeResult.try_error()));
    }

    if (GetWindowRect(a_window, &windowRectangle) == FALSE)
    {
        return cue::Result<void>::failure(make_native_error(a_assertContext, k_errorDomain, k_windowOperationFailed,
                                                            k_rectangleQueryFailed, GetLastError()));
    }

    const cue::WindowSize clientSize = *clientSizeResult.try_value();
    const std::int64_t nonClientWidth =
        (static_cast<std::int64_t>(windowRectangle.right) - static_cast<std::int64_t>(windowRectangle.left)) -
        static_cast<std::int64_t>(clientSize.width);
    const std::int64_t nonClientHeight =
        (static_cast<std::int64_t>(windowRectangle.bottom) - static_cast<std::int64_t>(windowRectangle.top)) -
        static_cast<std::int64_t>(clientSize.height);
    const std::int64_t width = static_cast<std::int64_t>(a_size.width) + nonClientWidth;
    const std::int64_t height = static_cast<std::int64_t>(a_size.height) + nonClientHeight;

    if (width <= 0 || height <= 0 || width > std::numeric_limits<int>::max() ||
        height > std::numeric_limits<int>::max())
    {
        return cue::Result<void>::failure(
            make_native_error(a_assertContext, k_errorDomain, k_windowOperationFailed,
                              "Windows Window Lifecycle Probe received an unsupported Client Size",
                              ERROR_INVALID_PARAMETER));
    }

    if (SetWindowPos(a_window, nullptr, 0, 0, static_cast<int>(width), static_cast<int>(height),
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) == FALSE)
    {
        return cue::Result<void>::failure(
            make_native_error(a_assertContext, k_errorDomain, k_windowOperationFailed,
                              "Windows Window Lifecycle Probe could not resize the Window", GetLastError()));
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
