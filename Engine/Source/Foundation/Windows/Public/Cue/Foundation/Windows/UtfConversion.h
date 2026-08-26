#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace cue
{
class EmergencyHandler;

static_assert(sizeof(wchar_t) == 2, "Cue.Foundation.Windows requires 16-bit wchar_t");

/// @brief Windows UTF 変換の完了状態
enum class WindowsUtfConversionStatus : std::uint8_t
{
    Success,
    InputTooLong,
    InvalidSequence,
    NativeFailure,
};

/// @brief Windows UTF 変換の非所有診断結果
///
/// Error Domain の意味付けを呼び出し側に残すため、Allocation 不要な状態と Win32 Code だけを保持する
struct WindowsUtfConversionResult final
{
    WindowsUtfConversionStatus status;
    std::int64_t nativeCode;
};

/// @brief 指定 Code Unit 数を Win32 UTF 変換 API の符号付き長さで表現できるか判定する
[[nodiscard]] bool is_windows_utf_conversion_length_supported(std::size_t a_length) noexcept;

/// @brief UTF-8 を Windows UTF-16 へ厳密変換する
/// @param a_text 呼び出し中だけ参照する UTF-8 文字列。埋め込み NUL も入力の一部として扱う
/// @param a_output 呼び出し側が所有する出力。失敗時は空になる
/// @param a_emergencyHandler Allocation 失敗時の非所有終了境界
/// @return 成功状態、または呼び出し側が Error へ変換する失敗状態と Win32 Code
[[nodiscard]] WindowsUtfConversionResult convert_utf8_to_windows_utf16(
    std::string_view a_text, std::wstring &a_output, EmergencyHandler &a_emergencyHandler) noexcept;

/// @brief Windows UTF-16 を UTF-8 へ厳密変換する
/// @param a_text 呼び出し中だけ参照する UTF-16 文字列。埋め込み NUL も入力の一部として扱う
/// @param a_output 呼び出し側が所有する出力。失敗時は空になる
/// @param a_emergencyHandler Allocation 失敗時の非所有終了境界
/// @return 成功状態、または呼び出し側が Error へ変換する失敗状態と Win32 Code
[[nodiscard]] WindowsUtfConversionResult convert_windows_utf16_to_utf8(
    std::wstring_view a_text, std::string &a_output, EmergencyHandler &a_emergencyHandler) noexcept;
} // namespace cue
