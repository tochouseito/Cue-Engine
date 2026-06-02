// PlatformFactory の役割と公開要素を定義する

#pragma once

// === C++ includes ===
#include <memory>

namespace Cue::PAL
{
    class IPlatform;

    /// @brief 現在の環境に応じたプラットフォーム実装を生成する
    /// @return 生成したプラットフォーム実装
    std::unique_ptr<IPlatform> create_platform();
}
