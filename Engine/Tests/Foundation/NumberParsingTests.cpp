#include <Cue/Foundation/NumberParsing.h>

#include <cstdint>
#include <limits>
#include <string_view>

namespace
{
/// @brief ASCIIとWide文字列の有効な10進表現を、指定した符号なし整数型へ変換できることを検証する
[[nodiscard]] bool test_valid_values()
{
    constexpr auto zero = cue::parse_unsigned_decimal<std::uint32_t>(std::string_view("0"));
    constexpr auto maximum =
        cue::parse_unsigned_decimal<std::uint32_t>(std::wstring_view(L"4294967295"));
    constexpr auto byte = cue::parse_unsigned_decimal<std::uint8_t>(std::string_view("255"));

    return zero.has_value() && *zero == 0 && maximum.has_value() &&
           *maximum == std::numeric_limits<std::uint32_t>::max() && byte.has_value() && *byte == 255;
}

/// @brief 空文字、符号、空白、10進数字以外を変換せず、失敗を空のOptionalとして返すことを検証する
[[nodiscard]] bool test_invalid_characters()
{
    return !cue::parse_unsigned_decimal<std::uint32_t>(std::string_view()).has_value() &&
           !cue::parse_unsigned_decimal<std::uint32_t>(std::string_view("+1")).has_value() &&
           !cue::parse_unsigned_decimal<std::uint32_t>(std::string_view(" 1")).has_value() &&
           !cue::parse_unsigned_decimal<std::uint32_t>(std::string_view("1a")).has_value();
}

/// @brief 変換先整数型の最大値を超える10進表現をWrapせず失敗として返すことを検証する
[[nodiscard]] bool test_overflow()
{
    return !cue::parse_unsigned_decimal<std::uint32_t>(std::string_view("4294967296")).has_value() &&
           !cue::parse_unsigned_decimal<std::uint8_t>(std::wstring_view(L"256")).has_value();
}
} // namespace

/// @brief 安全な10進符号なし整数変換の正常入力、無効文字、Overflow契約を検証して終了Codeを返す
int main()
{
    return test_valid_values() && test_invalid_characters() && test_overflow() ? 0 : 1;
}
