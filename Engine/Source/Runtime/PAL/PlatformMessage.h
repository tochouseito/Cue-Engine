#pragma once

// === C++ includes ===
#include <cstdint>

namespace Cue::PAL
{
    /// @brief プラットフォーム層から返るメッセージ種別です。
    enum class PlatformMessage : uint8_t
    {
        None = 0,
        Quit,
    };
}
