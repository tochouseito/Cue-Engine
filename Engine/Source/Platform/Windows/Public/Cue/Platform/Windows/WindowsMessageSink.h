#pragma once

#include <Cue/Foundation/Result.h>
#include <Cue/Platform/Window.h>

#include <cstdint>

namespace cue
{
class AssertContext;

/// @brief Win32 Message Sink境界の回復可能な失敗分類
enum class WindowsMessageSinkError : std::int64_t
{
    InvalidWindowKind = 13,
    MessageSinkUnavailable = 14,
    MessageSinkAlreadyAttached = 15,
    MessageSinkMismatch = 16,
};

/// @brief Windows Headerを公開せず同期Message配送に必要な値だけを渡すView
struct WindowsMessageView final
{
    const void *nativeWindow;
    std::uint32_t message;
    std::uintptr_t wordParameter;
    std::intptr_t longParameter;
};

/// @brief Win32の既定処理を抑止するかとNative Resultを返す値
struct WindowsMessageResult final
{
    bool isHandled;
    std::intptr_t nativeResult;
};

/// @brief Tool UIへWin32 Messageを同期配送する非所有Callback境界
class WindowsMessageSink
{
  public:
    WindowsMessageSink(const WindowsMessageSink &) = delete;
    WindowsMessageSink &operator=(const WindowsMessageSink &) = delete;

    /// @brief 派生SinkをWindowsWindowより先に破棄できない所有契約を提供する
    virtual ~WindowsMessageSink() = default;

    /// @brief Platformが所有しないWin32 Messageを処理し、既定処理の要否を返す
    [[nodiscard]] virtual WindowsMessageResult process_message(const WindowsMessageView &a_message) noexcept = 0;

  protected:
    /// @brief 非所有Callback境界だけを派生実装へ提供する
    WindowsMessageSink() noexcept = default;
};

/// @brief Windows WindowへOwner Thread限定の非所有Message Sinkを関連付ける
///
/// a_sinkはdetach完了までWindowより長く生存する
[[nodiscard]] Result<void> attach_windows_message_sink(Window &a_window, WindowsMessageSink &a_sink,
                                                       const AssertContext &a_assertContext) noexcept;

/// @brief Windows Windowから同一Message Sinkの関連付けを解除する
[[nodiscard]] Result<void> detach_windows_message_sink(Window &a_window, WindowsMessageSink &a_sink,
                                                       const AssertContext &a_assertContext) noexcept;
} // namespace cue
