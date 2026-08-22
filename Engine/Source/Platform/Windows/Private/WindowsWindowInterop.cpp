#include <Cue/Platform/Windows/WindowsWindowInterop.h>

#include "WindowsWindow.h"

#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Error.h>

#include <cstdint>
#include <utility>

namespace
{
constexpr std::int64_t k_foreignWindow = 12;

[[nodiscard]] cue::Error make_error(const cue::AssertContext &a_context, std::string_view a_summary) noexcept
{
    cue::ErrorCode code = cue::ErrorCode::create(a_context.fatal_handler(), "Cue.Platform.Windows", k_foreignWindow);
    return cue::Error::create(a_context.fatal_handler(), std::move(code), a_summary);
}
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
    WindowsWindow *window = dynamic_cast<WindowsWindow *>(&a_window);

    if (window == nullptr)
    {
        return Result<NativeWindowView>::failure(
            make_error(a_assertContext, "Native Win32 View requires a Windows Window"));
    }

    return Result<NativeWindowView>::success(NativeWindowView(window->native_view_value()));
}
} // namespace cue
