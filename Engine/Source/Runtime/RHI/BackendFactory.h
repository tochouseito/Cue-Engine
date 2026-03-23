#pragma once

// === C++ includes ===
#include <memory>

namespace Cue::RHI
{
    class IBackend;

    /// @brief 利用可能な RHI バックエンドを生成します。
    std::unique_ptr<IBackend> create_backend();
}
