// BackendFactory の役割と公開要素を定義する

#pragma once

// === C++ includes ===
#include <memory>

namespace Cue::RHI
{
    class IBackend;

    /// @brief 利用可能な RHI バックエンドを生成する
    std::unique_ptr<IBackend> create_backend();
}
