#pragma once

#include <Cue/Foundation/Result.h>
#include <Cue/Platform/Window.h>

namespace cue
{
class AssertContext;

/// @brief Native Window View の Platform 種別
enum class NativeWindowKind
{
    /// Win32 API の HWND を Opaque 値として参照する View
    Win32,
};

/// @brief Windows 固有操作へ一時的に渡す非所有 Native Window View
///
/// RHI など Native Handle が不可欠な境界だけに Win32 Window を公開し、通常の Runtime 処理を
/// Platform 非依存に保つ
/// View は取得直後の Interop 呼出中だけ有効とし、保存、破棄、Close、Subclass 化に使用しない
class NativeWindowView final
{
  public:
    /// @brief Native Window の Platform 種別を返す
    [[nodiscard]] NativeWindowKind kind() const noexcept;

    /// @brief Native Window Handle を Opaque な非所有値として返す
    [[nodiscard]] const void *value() const noexcept;

  private:
    friend Result<NativeWindowView> get_native_window_view(Window &a_window,
                                                           const AssertContext &a_assertContext) noexcept;

    /// @brief NativeWindowView を必要な依存と初期状態から構築する
    explicit NativeWindowView(const void *a_value) noexcept;

    // Windows Header を公開 API に含めず HWND を渡すための非所有 Opaque 値
    const void *m_value;
};

/// @brief Windows Window の短命な Native View を取得する
///
/// a_window は create_windows_window_system() から生成し、Window Thread 上の Created、Visible、
/// CloseRequested 状態で呼び出す
/// @param a_assertContext 診断に使用する Context
/// @return Windows Window の短命な View、または異種 Window を示す Error
[[nodiscard]] Result<NativeWindowView> get_native_window_view(Window &a_window,
                                                              const AssertContext &a_assertContext) noexcept;
} // namespace cue
