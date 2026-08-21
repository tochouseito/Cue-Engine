#pragma once

#include <cstdint>

namespace cue
{
/** @brief Platform Window から通知される Event 種別 */
enum class WindowEventType
{
    CloseRequested,
    Resized,
    Minimized,
    Restored,
    Destroyed,
};

/** @brief Window Client Area の Size */
struct WindowSize final
{
    std::uint32_t width;
    std::uint32_t height;
};

/**
 * @brief Platform Window Event の値表現
 *
 * Resized と Restored だけが 0 以外の clientSize を持ち、その他の Event
 * では clientSize を参照しない
 */
struct WindowEvent final
{
    WindowEventType type;
    WindowSize clientSize;
};
} // namespace cue
