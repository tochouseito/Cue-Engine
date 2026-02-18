#pragma once
#include <cstdint>
#include "TimeUnit.h"

namespace Cue::Math
{
    /// @brief 値を指定された倍数に切り上げます。
    /// @param value 切り上げる値。
    /// @param step 切り上げ先の倍数。0の場合は無効です。
    /// @return 指定された倍数に切り上げられた値。
    [[nodiscard]] static constexpr uint32_t round_up_to_multiple(uint32_t value, uint32_t step) noexcept;
}
