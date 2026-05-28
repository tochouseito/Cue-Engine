// IClock の役割と公開要素を定義する

#pragma once

// === C++ includes ===
#include <cstdint>

// === Math includes ===
#include <TimeUnit.h>

namespace Cue::Core::Time
{
    /// @brief 現在時刻を返すクロックの抽象インターフェース
    class IClock
    {
    public:
        IClock() = default;
        virtual ~IClock() = default;

        /// @brief ナノ秒単位の現在時刻を返す
        [[nodiscard]] virtual Math::TimeSpan now_ns() const noexcept = 0;
    };
}
