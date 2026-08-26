#include <Cue/Platform/Windows/WindowsWindowInterop.h>

#include "WindowsWindow.h"
#include "WindowsUtilities.h"

#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Error.h>

#include <cstdint>
#include <utility>

namespace
{
using cue::windows_private::make_error;

constexpr std::int64_t k_foreignWindow = 12;
} // namespace

namespace cue
{
NativeWindowView::NativeWindowView(const void *a_value) noexcept : m_value(a_value)
{
}

NativeWindowKind NativeWindowView::kind() const noexcept
{
    return NativeWindowKind::Win32;
}

const void *NativeWindowView::value() const noexcept
{
    return m_value;
}

Result<NativeWindowView> get_native_window_view(Window &a_window, const AssertContext &a_assertContext) noexcept
{
    // Opaque な共通 Window が本実装の所有物であることを確認し、異種 Handle の誤用を防ぐ
    WindowsWindow *window = dynamic_cast<WindowsWindow *>(&a_window);

    if (window == nullptr)
    {
        return Result<NativeWindowView>::failure(
            make_error(a_assertContext, k_foreignWindow, "Native Win32 View requires a Windows Window"));
    }

    // 非所有 View だけを返し、Interop 呼出側へ HWND の破棄責務を移さない
    return Result<NativeWindowView>::success(NativeWindowView(window->native_view_value()));
}
} // namespace cue
