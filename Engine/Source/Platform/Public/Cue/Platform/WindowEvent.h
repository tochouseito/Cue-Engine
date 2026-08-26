#pragma once

#include <cstdint>

namespace cue
{
/// @brief Platform Window から通知される Event 種別
enum class WindowEventType
{
    /// OS の閉じる操作を Runtime 側の終了判断へ委ねるための要求
    CloseRequested,
    /// 描画対象の Client Area が有効な大きさへ変化した通知
    Resized,
    /// 描画を休止できるよう Client Area が最小化されたことを示す通知
    Minimized,
    /// 最小化から復帰し、新しい Client Area で描画を再開できることを示す通知
    Restored,
    /// Native Window の寿命が終了し、以後操作できないことを示す通知
    Destroyed,
};

/// @brief Window Client Area の Size
struct WindowSize final
{
    /// 描画可能な Client Area の横幅
    std::uint32_t width;
    /// 描画可能な Client Area の縦幅
    std::uint32_t height;
};

/// @brief Platform Window Event の値表現
///
/// Resized と Restored だけが 0 以外の clientSize を持ち、その他の Event
/// では clientSize を参照しない
struct WindowEvent final
{
    WindowEventType type;
    WindowSize clientSize;
};
} // namespace cue
