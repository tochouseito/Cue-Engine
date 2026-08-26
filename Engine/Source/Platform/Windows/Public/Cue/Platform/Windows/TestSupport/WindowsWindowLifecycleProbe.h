#pragma once

#include <Cue/Foundation/Result.h>
#include <Cue/Platform/WindowEvent.h>

namespace cue
{
class AssertContext;
class Window;

/// @brief RuntimeHostの実Window Lifecycle Smokeで発行するWindows操作
enum class WindowsWindowLifecycleProbeAction
{
    Resize,
    Minimize,
    Restore,
    ResizeThenClose,
};

/// @brief Windows WindowへLifecycle Smoke操作を同期発行する
///
/// Native Window Message CallbackはEventを格納するだけで、GPU操作は行わない。
/// Resize系Actionはa_firstSize、a_finalSizeの順に連続変更する。
[[nodiscard]] Result<void> issue_windows_window_lifecycle_probe_action(
    Window &a_window, WindowsWindowLifecycleProbeAction a_action, WindowSize a_firstSize,
    WindowSize a_finalSize, const AssertContext &a_assertContext) noexcept;
} // namespace cue
