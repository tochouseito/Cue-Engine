// AudioBackendFactory の役割と公開要素を定義する

#pragma once

// === Audio includes ===
#include "Audio.h"

// === C++ includes ===
#include <memory>

namespace Cue::Audio
{
    /// @brief 現在の環境に応じた Audio backend を生成する
    std::unique_ptr<IBackend> create_backend();
}
