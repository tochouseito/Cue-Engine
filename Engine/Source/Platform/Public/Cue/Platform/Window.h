#pragma once

#include <Cue/Foundation/Result.h>
#include <Cue/Platform/WindowEvent.h>

namespace cue
{
/** @brief Platform Window の Lifecycle 状態 */
enum class WindowState
{
    Created,
    Visible,
    CloseRequested,
    Destroyed,
};

/**
 * @brief Platform 非依存の Window 操作契約
 *
 * 全 API と Destructor は Window System の作成 Thread 上で呼び出す
 */
class Window
{
  public:
    /** @brief Window Thread 上で Owner を破棄する */
    virtual ~Window() noexcept;

    /** @brief Native Window を表示する */
    [[nodiscard]] virtual Result<void> show() noexcept = 0;

    /** @brief Native Window を明示的に破棄する */
    [[nodiscard]] virtual Result<void> destroy() noexcept = 0;

    /** @brief 現在の Lifecycle 状態を返す */
    [[nodiscard]] virtual WindowState state() const noexcept = 0;

    /** @brief 現在の Client Area Size を返す */
    [[nodiscard]] virtual WindowSize client_size() const noexcept = 0;

    /**
     * @brief FIFO Queue の先頭 Event を取得する
     * @return Event を取得した場合は true、Queue が空の場合は false
     */
    [[nodiscard]] virtual bool try_pop_event(WindowEvent &a_event) noexcept = 0;
};
} // namespace cue
