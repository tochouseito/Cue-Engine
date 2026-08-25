#pragma once

#include <Cue/Foundation/Result.h>
#include <Cue/Platform/WindowEvent.h>

namespace cue
{
/** @brief Platform Window の Lifecycle 状態 */
enum class WindowState
{
    /** Native View や SwapChain の表示前準備を行えるよう、生成と画面表示を分離した状態 */
    Created,
    /** Native Window を画面へ表示した状態 */
    Visible,
    /** 閉じる要求を通知済みで、Runtime 側の破棄判断を待つ状態 */
    CloseRequested,
    /** Native Window の寿命が終了し、Native View を取得できない状態 */
    Destroyed,
};

/**
 * @brief Platform 非依存の Window 操作契約
 *
 * 全 API と Destructor は Window System の作成 Thread 上で呼び出す
 *
 * Native Callback は再入による Runtime 状態変更を避けるため、通常の Runtime Event Callback を直接呼ばず
 * Event を Window 所有の FIFO Queue へ値として格納する
 * 診断と回復不能 Error の経路では Logger または FatalHandler を同期呼び出しする場合がある
 * Queue は Window と同時に破棄され、取得済み Event だけが Window 破棄後も呼出側で保持できる
 */
class Window
{
  public:
    /** @brief Window Thread 上で Owner を破棄する */
    virtual ~Window() noexcept;

    /** @brief 生成を完了した Native Window を Runtime の操作対象として表示する */
    [[nodiscard]] virtual Result<void> show() noexcept = 0;

    /** @brief Runtime が終了時点を決め、Native Window を明示的に破棄する */
    [[nodiscard]] virtual Result<void> destroy() noexcept = 0;

    /** @brief 現在の Lifecycle 状態を返す */
    [[nodiscard]] virtual WindowState state() const noexcept = 0;

    /** @brief 現在の Client Area Size を返す */
    [[nodiscard]] virtual WindowSize client_size() const noexcept = 0;

    /**
     * @brief FIFO Queue の先頭 Event を取得する
     * @return Event を取得した場合は true、Queue が空の場合は false
     *
     * Queue が空の場合は a_event を変更しない
     */
    [[nodiscard]] virtual bool try_pop_event(WindowEvent &a_event) noexcept = 0;
};
} // namespace cue
