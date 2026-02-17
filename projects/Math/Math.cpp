#include "math_pch.cpp"
#include "Math.h"

namespace Cue::Math
{
    [[nodiscard]]
    constexpr uint32_t round_up_to_multiple(uint32_t value, uint32_t step) noexcept
    {
        uint32_t out = 0;

        // 1) step == 0 は無効
        if(step <= 0)
        {
            Core::LogAssert::cue_assert(
                false,
                Core::LogSink::debugConsole,
                "step must be greater than 0.");
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
            Core::LogAssert::cue_assert(
                false,
                Core::LogSink::debugConsole,
                "Overflow detected.");
        }

        out = value + add;
        return out;
    }
}
