#include <Cue/Platform/Windows/WindowsWindowInterop.h>

#include "WindowsWindow.h"

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

Result<NativeWindowView> get_native_window_view(Window &a_window) noexcept
{
    WindowsWindow &window = static_cast<WindowsWindow &>(a_window);
    return Result<NativeWindowView>::success(NativeWindowView(window.native_view_value()));
}
} // namespace cue
