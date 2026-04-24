#pragma once

// === Audio includes ===
#include "Audio.h"

// === C++ includes ===
#include <memory>

namespace Cue::Audio
{
    /// @brief 現在の環境に応じた Audio backend を生成します。
    std::unique_ptr<IBackend> create_backend();
}
