#pragma once

#include <Cue/Foundation/Result.h>
#include <Cue/Platform/Window.h>

namespace cue
{
class AssertContext;

/** @brief Native Window ViewのPlatform種別 */
enum class NativeWindowKind
{
    Win32,
};

/**
 * @brief Windows固有操作へ一時的に渡す非所有Native Window View
 *
 * Viewは取得直後のInterop呼出中だけ有効とし、保存、破棄、Close、Subclass化に使用しない
 */
class NativeWindowView final
{
  public:
    /** @brief Native WindowのPlatform種別を返す */
    [[nodiscard]] NativeWindowKind kind() const noexcept;

    /** @brief Native Window HandleをOpaqueな非所有値として返す */
    [[nodiscard]] const void *value() const noexcept;

  private:
    friend Result<NativeWindowView> get_native_window_view(Window &a_window,
                                                           const AssertContext &a_assertContext) noexcept;

    explicit NativeWindowView(const void *a_value) noexcept;

    const void *m_value;
};

/**
 * @brief Windows Windowの短命なNative Viewを取得する
 *
 * a_windowはcreate_windows_window_system()から生成し、Window Thread上のCreated、Visible、
 *
 * CloseRequested状態で呼び出す
 * @param a_assertContext 診断に使用する Context
 * @return Windows Window の短命な
 * View、または異種 Window を示す Error
 */
[[nodiscard]] Result<NativeWindowView> get_native_window_view(Window &a_window,
                                                              const AssertContext &a_assertContext) noexcept;
} // namespace cue
