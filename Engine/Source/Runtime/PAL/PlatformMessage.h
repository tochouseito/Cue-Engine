#pragma once

// C++ includes
#include <cstdint>

namespace Cue::PAL
{
    // プラットフォームからのメッセージ
    enum class PlatformMessage : uint8_t
    {
        None = 0,
        Quit,
    };
}
