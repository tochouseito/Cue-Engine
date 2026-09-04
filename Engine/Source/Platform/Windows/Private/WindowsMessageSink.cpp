#include <Cue/Platform/Windows/WindowsMessageSink.h>

#include "WindowsUtilities.h"
#include "WindowsWindow.h"

#include <Cue/Foundation/Assert.h>

#include <utility>

namespace
{
using cue::windows_private::make_error;

/// @brief Message Sink Error分類を安定したPlatform Windows Errorへ変換する
[[nodiscard]] cue::Error make_sink_error(const cue::AssertContext &a_context,
                                         cue::WindowsMessageSinkError a_error,
                                         std::string_view a_summary) noexcept
{
    return make_error(a_context, static_cast<std::int64_t>(a_error), a_summary);
}
} // namespace

namespace cue
{
Result<void> attach_windows_message_sink(Window &a_window, WindowsMessageSink &a_sink,
                                         const AssertContext &a_assertContext) noexcept
{
    WindowsWindow *window = dynamic_cast<WindowsWindow *>(&a_window);
    if (window == nullptr)
    {
        return Result<void>::failure(make_sink_error(a_assertContext, WindowsMessageSinkError::InvalidWindowKind,
                                                     "Message Sink requires a Windows Window"));
    }
    return window->attach_message_sink(a_sink);
}

Result<void> detach_windows_message_sink(Window &a_window, WindowsMessageSink &a_sink,
                                         const AssertContext &a_assertContext) noexcept
{
    WindowsWindow *window = dynamic_cast<WindowsWindow *>(&a_window);
    if (window == nullptr)
    {
        return Result<void>::failure(make_sink_error(a_assertContext, WindowsMessageSinkError::InvalidWindowKind,
                                                     "Message Sink requires a Windows Window"));
    }
    return window->detach_message_sink(a_sink);
}

Result<void> WindowsWindow::attach_message_sink(WindowsMessageSink &a_sink) noexcept
{
    verify_thread();
    CUE_ASSERT(m_system->assert_context(), !m_isDispatchingMessageSink,
               "Message Sink association cannot change during callback dispatch");
    CUE_ASSERT(m_system->assert_context(), m_state == WindowState::Created || m_state == WindowState::Visible,
               "Message Sink can only attach to a Created or Visible Window");

    if (m_window == nullptr)
    {
        return Result<void>::failure(make_sink_error(m_system->assert_context(),
                                                     WindowsMessageSinkError::MessageSinkUnavailable,
                                                     "Windows Message Sink target is unavailable"));
    }
    if (m_messageSink == &a_sink)
    {
        return Result<void>::success();
    }
    if (m_messageSink != nullptr)
    {
        return Result<void>::failure(make_sink_error(m_system->assert_context(),
                                                     WindowsMessageSinkError::MessageSinkAlreadyAttached,
                                                     "Windows Message Sink is already attached"));
    }

    m_messageSink = &a_sink;
    return Result<void>::success();
}

Result<void> WindowsWindow::detach_message_sink(WindowsMessageSink &a_sink) noexcept
{
    verify_thread();
    CUE_ASSERT(m_system->assert_context(), !m_isDispatchingMessageSink,
               "Message Sink association cannot change during callback dispatch");
    CUE_ASSERT(m_system->assert_context(), m_state == WindowState::Created || m_state == WindowState::Visible ||
                                                    m_state == WindowState::CloseRequested,
               "Message Sink can only detach from a live Window");

    if (m_window == nullptr)
    {
        return Result<void>::failure(make_sink_error(m_system->assert_context(),
                                                     WindowsMessageSinkError::MessageSinkUnavailable,
                                                     "Windows Message Sink target is unavailable"));
    }
    if (m_messageSink == nullptr)
    {
        return Result<void>::success();
    }
    if (m_messageSink != &a_sink)
    {
        return Result<void>::failure(make_sink_error(m_system->assert_context(),
                                                     WindowsMessageSinkError::MessageSinkMismatch,
                                                     "Windows Message Sink does not match the attached Sink"));
    }

    m_messageSink = nullptr;
    return Result<void>::success();
}
} // namespace cue
