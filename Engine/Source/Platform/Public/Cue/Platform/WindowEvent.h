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

/** @brief Platform Window Event の値表現 */
struct WindowEvent final
{
    WindowEventType type;
    WindowSize clientSize;
};
} // namespace cue
