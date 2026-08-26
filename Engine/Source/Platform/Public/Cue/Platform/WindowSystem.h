#pragma once

#include <Cue/Foundation/Result.h>
#include <Cue/Platform/Window.h>

#include <memory>
#include <string_view>

namespace cue
{
/// @brief Window 生成時の Platform 非依存 Descriptor
///
/// title は create_window() 呼出中だけ有効な非所有 View とし、実装は呼出後に保持しない
struct WindowDescriptor final
{
    /// Platform API へ渡す前に Native 文字表現へ変換される UTF-8 の表示名
    std::string_view title;
    /// Window Frame を含まない描画領域として要求する Size
    WindowSize clientSize;
};

/// @brief Non-blocking Message Pump の結果
enum class PumpStatus
{
    /// Message Queue を処理した後も Runtime Loop を継続できる状態
    Running,
    /// OS の Thread 終了要求を観測し、Runtime Loop を終了すべき状態
    QuitRequested,
};

/// @brief Platform 非依存の Window System 契約
///
/// 全 API と Destructor は作成 Thread 上で呼び出し、生成した全 Window より長く生存させる
class WindowSystem
{
  public:
    /// @brief Window Thread 上で Owner を破棄する
    virtual ~WindowSystem() noexcept;

    /// @brief Window の一意な所有権を生成する
    /// @param a_descriptor 呼出中だけ借用する生成情報
    /// @return 生成した Window、または検証・Platform 処理の Error
    ///
    /// M02 は単一 Main Window だけを許可し、既存 Window がある場合は Error を返す
    /// 失敗時は Native Ownership と Window Class 参照を呼出前の状態へ戻し、再試行を許可する
    [[nodiscard]] virtual Result<std::unique_ptr<Window>> create_window(
        const WindowDescriptor &a_descriptor) noexcept = 0;

    /// @brief 現在の Thread Queue が空になるまで Non-blocking で処理する
    /// @return WM_QUIT を含めて Queue を Drain した場合は QuitRequested、それ以外は Running
    ///
    /// Runtime の Frame Loop を止めずに OS 応答性を維持するため、一件を待つ Blocking Pump にはしない
    [[nodiscard]] virtual Result<PumpStatus> pump_events() noexcept = 0;
};
} // namespace cue
