// PlatformMessage の役割と公開要素を定義する

#pragma once

// === C++ includes ===
#include <cstdint>

namespace Cue::PAL
{
    /// @brief プラットフォーム層から返るメッセージ種別
    enum class PlatformMessage : uint8_t
    {
        None = 0,
        Quit,
    };
}
