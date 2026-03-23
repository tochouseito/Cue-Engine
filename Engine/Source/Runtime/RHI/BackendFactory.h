#pragma once

// === C++ includes ===
#include <memory>

namespace Cue::RHI
{
    class IBackend;

    /// <summary>
    /// 利用可能な RHI バックエンドを生成します。
    /// </summary>
    std::unique_ptr<IBackend> create_backend();
}
