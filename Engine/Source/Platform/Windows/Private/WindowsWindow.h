#pragma once

#include <Cue/Platform/WindowSystem.h>

#include <Windows.h>

#include <cstdint>
#include <memory>
#include <string_view>

namespace cue
{
class AssertContext;
class WindowsWindow;

class WindowsWindowSystem final : public WindowSystem
{
  public:
    WindowsWindowSystem(const AssertContext &a_assertContext, HINSTANCE a_instance) noexcept;
    ~WindowsWindowSystem() noexcept override;

    [[nodiscard]] Result<std::unique_ptr<Window>> create_window(const WindowDescriptor &a_descriptor) noexcept override;
    [[nodiscard]] Result<PumpStatus> pump_events() noexcept override;

    [[nodiscard]] const AssertContext &assert_context() const noexcept;
    [[nodiscard]] HINSTANCE instance() const noexcept;
    [[nodiscard]] DWORD thread_id() const noexcept;
    void publish_window(WindowsWindow &a_window) noexcept;
    void release_window(WindowsWindow &a_window) noexcept;

  private:
    [[nodiscard]] Result<void> register_window_class() noexcept;
    void unregister_window_class() noexcept;

    const AssertContext *m_assertContext;
    HINSTANCE m_instance;
    DWORD m_threadId;
    WindowsWindow *m_window = nullptr;
    bool m_isClassRegistered = false;
};

class WindowsWindow final : public Window
{
  public:
    explicit WindowsWindow(WindowsWindowSystem &a_system) noexcept;
    ~WindowsWindow() noexcept override;

    [[nodiscard]] Result<void> show() noexcept override;
    [[nodiscard]] Result<void> destroy() noexcept override;
    [[nodiscard]] WindowState state() const noexcept override;
    [[nodiscard]] WindowSize client_size() const noexcept override;
    [[nodiscard]] bool try_pop_event(WindowEvent &a_event) noexcept override;
    [[nodiscard]] const void *native_view_value() const noexcept;

    void acquire_class_reference() noexcept;
    [[nodiscard]] Result<void> create_native(std::wstring_view a_title, int a_width, int a_height) noexcept;
    void publish() noexcept;

    [[nodiscard]] static LRESULT CALLBACK window_procedure(HWND a_window, UINT a_message, WPARAM a_wParam,
                                                           LPARAM a_lParam) noexcept;

  private:
    [[nodiscard]] LRESULT process_message(UINT a_message, WPARAM a_wParam, LPARAM a_lParam) noexcept;
    void release_system_reference() noexcept;
    void verify_thread() const noexcept;

    WindowsWindowSystem *m_system;
    HWND m_window = nullptr;
    WindowSize m_clientSize = {};
    WindowState m_state = WindowState::Destroyed;
    bool m_hasClassReference = false;
    bool m_isPublished = false;
};
} // namespace cue
