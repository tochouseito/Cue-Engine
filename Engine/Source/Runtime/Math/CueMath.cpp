#include "Math_pch.h"
#include "CueMath.h"

namespace Cue::Math
{
    [[nodiscard]]
    uint32_t round_up_to_multiple(uint32_t a_value, uint32_t a_step) noexcept
    {
        uint32_t out = 0;

        // - 0 の倍数への切り上げは定義できないため停止する
        if (a_step == 0)
        {
            CUE_ASSERT_FORMAT(false, "Invalid step: %u. Step must be greater than 0.", a_step);
        }

        // - 既に倍数ならそのまま
        const uint32_t r = a_value % a_step;
        if (r == 0)
        {
            out = a_value;
            return out;
        }

        // 次の倍数へ（オーバーフロー検出）
        const uint32_t add = a_step - r;
        const uint32_t maxValue = (std::numeric_limits<uint32_t>::max)();
        // a_value + add > maxValue ならオーバーフローする
        if (a_value > (maxValue - add))
        {
            CUE_ASSERT_FORMAT(false, "Overflow detected: value %u + add %u exceeds max uint32_t %u.", a_value, add, maxValue);
        }

        out = a_value + add;
        return out;
    }
}
