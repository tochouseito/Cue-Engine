#include "math_pch.cpp"
#include "CueMath.h"

namespace Cue::Math
{
    [[nodiscard]]
    constexpr uint32_t round_up_to_multiple(uint32_t value, uint32_t step) noexcept
    {
        uint32_t out = 0;

        // 1) step == 0 は無効
        if(step <= 0)
        {
            Assert::cue_assert(
                false,
                "Invalid step: {}. Step must be greater than 0.",
                step);
        }

        // 2) 既に倍数ならそのまま
        const uint32_t r = value % step;
        if (r == 0)
        {
            out = value;
            return out;
        }

        // 3-0) 次の倍数へ（オーバーフロー検出）
        const uint32_t add = step - r;
        const uint32_t maxv = (std::numeric_limits<uint32_t>::max)();
        // 3-1) value + add > maxv ならオーバーフローする
        if (value > (maxv - add))
        {
            Assert::cue_assert(
                false,
                "Overflow detected: value {} + add {} exceeds max uint32_t {}.",
                value, add, maxv);
        }

        out = value + add;
        return out;
    }
}
