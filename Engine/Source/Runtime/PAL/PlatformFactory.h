#pragma once

// === C++ includes ===
#include <memory>

namespace Cue::PAL
{
    class IPlatform;

    /// @brief 現在の環境に応じたプラットフォーム実装を生成します。
    /// @return 生成したプラットフォーム実装です。
    std::unique_ptr<IPlatform> create_platform();
}
