#include <Cue/Foundation/EmergencyHandler.h>
#include <Cue/Foundation/Windows/UtfConversion.h>

#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>

namespace
{
class TestEmergencyHandler final : public cue::EmergencyHandler
{
  public:
    /// @brief Test 中の回復不能な Allocation 失敗を Process 終了として扱う
    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::abort();
    }
};

/// @brief 空入力が成功し、以前の出力を空へ置き換えることを検証する
[[nodiscard]] bool test_empty_input(TestEmergencyHandler &a_emergencyHandler)
{
    std::wstring utf16 = L"stale";
    std::string utf8 = "stale";
    const cue::WindowsUtfConversionResult toUtf16 =
        cue::convert_utf8_to_windows_utf16({}, utf16, a_emergencyHandler);
    const cue::WindowsUtfConversionResult toUtf8 =
        cue::convert_windows_utf16_to_utf8({}, utf8, a_emergencyHandler);
    return toUtf16.status == cue::WindowsUtfConversionStatus::Success &&
           toUtf8.status == cue::WindowsUtfConversionStatus::Success && utf16.empty() && utf8.empty();
}

/// @brief ASCII、日本語、補助平面文字を UTF-8 と UTF-16 の間で往復できることを検証する
[[nodiscard]] bool test_unicode_round_trip(TestEmergencyHandler &a_emergencyHandler)
{
    const std::string original = "Cue \xE6\x97\xA5\xE6\x9C\xAC \xF0\x9F\x98\x80";
    std::wstring utf16;
    std::string roundTrip;
    const cue::WindowsUtfConversionResult toUtf16 =
        cue::convert_utf8_to_windows_utf16(original, utf16, a_emergencyHandler);

    if (toUtf16.status != cue::WindowsUtfConversionStatus::Success)
    {
        return false;
    }

    const cue::WindowsUtfConversionResult toUtf8 =
        cue::convert_windows_utf16_to_utf8(utf16, roundTrip, a_emergencyHandler);
    return toUtf8.status == cue::WindowsUtfConversionStatus::Success && roundTrip == original;
}

/// @brief 埋め込み NUL を終端扱いせず、明示された長さの一部として往復できることを検証する
[[nodiscard]] bool test_embedded_null_round_trip(TestEmergencyHandler &a_emergencyHandler)
{
    const std::string original("A\0B", 3);
    std::wstring utf16;
    std::string roundTrip;
    const cue::WindowsUtfConversionResult toUtf16 =
        cue::convert_utf8_to_windows_utf16(original, utf16, a_emergencyHandler);

    if (toUtf16.status != cue::WindowsUtfConversionStatus::Success || utf16.size() != original.size())
    {
        return false;
    }

    const cue::WindowsUtfConversionResult toUtf8 =
        cue::convert_windows_utf16_to_utf8(utf16, roundTrip, a_emergencyHandler);
    return toUtf8.status == cue::WindowsUtfConversionStatus::Success && roundTrip == original;
}

/// @brief 不正な UTF-8 と UTF-16 を置換せず拒否し、出力を空にすることを検証する
[[nodiscard]] bool test_invalid_sequences(TestEmergencyHandler &a_emergencyHandler)
{
    const std::string invalidUtf8("\xC0\xAF", 2);
    const std::wstring invalidUtf16(1, static_cast<wchar_t>(0xD800));
    std::wstring utf16 = L"stale";
    std::string utf8 = "stale";
    const cue::WindowsUtfConversionResult toUtf16 =
        cue::convert_utf8_to_windows_utf16(invalidUtf8, utf16, a_emergencyHandler);
    const cue::WindowsUtfConversionResult toUtf8 =
        cue::convert_windows_utf16_to_utf8(invalidUtf16, utf8, a_emergencyHandler);
    return toUtf16.status == cue::WindowsUtfConversionStatus::InvalidSequence &&
           toUtf8.status == cue::WindowsUtfConversionStatus::InvalidSequence &&
           toUtf16.nativeCode != 0 && toUtf8.nativeCode != 0 && utf16.empty() && utf8.empty();
}

/// @brief Win32 の符号付き長さ上限を越える入力を Native 呼び出し前に判定できることを検証する
[[nodiscard]] bool test_input_length_boundary()
{
    const std::size_t maximumLength = static_cast<std::size_t>((std::numeric_limits<int>::max)());
    return cue::is_windows_utf_conversion_length_supported(maximumLength) &&
           !cue::is_windows_utf_conversion_length_supported(maximumLength + 1);
}
} // namespace

/// @brief WindowsUtfConversionTests の全契約を検証して Process 終了 Code を返す
int main()
{
    TestEmergencyHandler emergencyHandler;
    return test_empty_input(emergencyHandler) && test_unicode_round_trip(emergencyHandler) &&
                   test_embedded_null_round_trip(emergencyHandler) &&
                   test_invalid_sequences(emergencyHandler) && test_input_length_boundary()
               ? 0
               : 1;
}
