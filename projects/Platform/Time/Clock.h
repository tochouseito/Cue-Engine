#pragma once
#include <cstdint>

namespace Cue::Platform::Time
{
    class IClock
    {
    public:
        IClock() = default;
        virtual ~IClock() = default;
        // ナノ秒単位の現在時刻を取得する
        [[nodiscard]] virtual std::int64_t now_ns() const noexcept = 0;
    };
}
