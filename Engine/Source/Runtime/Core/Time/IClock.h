#pragma once

// === Math Includes ===
#include <TimeUnit.h>

// === C++ Includes ===
#include <cstdint>

namespace Cue::Core::Time
{
    class IClock
    {
    public:
        IClock() = default;
        virtual ~IClock() = default;
        // ナノ秒単位の現在時刻を取得する
        [[nodiscard]] virtual Math::TimeSpan now_ns() const noexcept = 0;
    };
}
