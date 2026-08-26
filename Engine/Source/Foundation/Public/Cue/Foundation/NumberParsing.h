#pragma once

#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>

namespace cue
{
/// @brief 符号や空白を許可しない10進文字列を符号なし整数へ変換し、無効入力またはOverflow時は空を返す
template <typename Integer, typename Character>
    requires(std::is_integral_v<Integer> && std::is_unsigned_v<Integer> && !std::is_same_v<Integer, bool>)
[[nodiscard]] constexpr std::optional<Integer> parse_unsigned_decimal(
    std::basic_string_view<Character> a_text) noexcept
{
    if (a_text.empty())
    {
        return std::nullopt;
    }

    constexpr Character k_zero = static_cast<Character>('0');
    constexpr Character k_nine = static_cast<Character>('9');
    constexpr Integer k_radix = 10;
    Integer value = 0;

    for (Character character : a_text)
    {
        if (character < k_zero || character > k_nine)
        {
            return std::nullopt;
        }

        Integer digit = static_cast<Integer>(character - k_zero);

        if (value > (std::numeric_limits<Integer>::max() - digit) / k_radix)
        {
            return std::nullopt;
        }

        value = value * k_radix + digit;
    }

    return value;
}
} // namespace cue
