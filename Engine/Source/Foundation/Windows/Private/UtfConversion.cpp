// Win32 の UTF 変換手順と境界条件を一箇所に集約し、上位 Module は機能固有の Error 意味付けだけを行う
// 明示長を使用して埋め込み NUL を保持し、不正 Sequence は代替文字へ置換せず失敗として返す

#include <Cue/Foundation/Windows/UtfConversion.h>

#include <Cue/Foundation/EmergencyHandler.h>

#include <Windows.h>

#include <cstdlib>
#include <limits>

namespace
{
/// @brief Allocation 失敗を追加 Allocation なしで Emergency 終了境界へ渡す
[[noreturn]] void terminate_allocation(cue::EmergencyHandler &a_emergencyHandler) noexcept
{
    a_emergencyHandler.terminate("Windows UTF conversion allocation failed");
    std::abort();
}

/// @brief 成功した Windows UTF 変換結果を生成する
[[nodiscard]] cue::WindowsUtfConversionResult make_success() noexcept
{
    return {cue::WindowsUtfConversionStatus::Success, ERROR_SUCCESS};
}

/// @brief 失敗した Windows UTF 変換結果を状態と Win32 Code から生成する
[[nodiscard]] cue::WindowsUtfConversionResult make_failure(cue::WindowsUtfConversionStatus a_status,
                                                            DWORD a_nativeCode) noexcept
{
    return {a_status, static_cast<std::int64_t>(a_nativeCode)};
}

/// @brief Win32 変換失敗を不正 Sequence または Native Failure に分類する
[[nodiscard]] cue::WindowsUtfConversionResult classify_native_failure(DWORD a_nativeCode) noexcept
{
    const cue::WindowsUtfConversionStatus status = a_nativeCode == ERROR_NO_UNICODE_TRANSLATION
                                                       ? cue::WindowsUtfConversionStatus::InvalidSequence
                                                       : cue::WindowsUtfConversionStatus::NativeFailure;
    return make_failure(status, a_nativeCode);
}
} // namespace

namespace cue
{
/// @brief 指定 Code Unit 数を Win32 UTF 変換 API の符号付き長さで表現できるか判定する
bool is_windows_utf_conversion_length_supported(std::size_t a_length) noexcept
{
    return a_length <= static_cast<std::size_t>((std::numeric_limits<int>::max)());
}

/// @brief UTF-8 を Windows UTF-16 へ厳密変換する
WindowsUtfConversionResult convert_utf8_to_windows_utf16(
    std::string_view a_text, std::wstring &a_output, EmergencyHandler &a_emergencyHandler) noexcept
{
    a_output.clear();

    if (a_text.empty())
    {
        return make_success();
    }

    if (!is_windows_utf_conversion_length_supported(a_text.size()))
    {
        return make_failure(WindowsUtfConversionStatus::InputTooLong, ERROR_INSUFFICIENT_BUFFER);
    }

    // Win32 の符号付き長さへ収めた後、終端文字ではなく String View 全体の必要量を問い合わせる
    const int sourceLength = static_cast<int>(a_text.size());
    const int convertedLength =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, a_text.data(), sourceLength, nullptr, 0);

    if (convertedLength == 0)
    {
        return classify_native_failure(GetLastError());
    }

    try
    {
        a_output.resize(static_cast<std::size_t>(convertedLength));
    }
    catch (...)
    {
        terminate_allocation(a_emergencyHandler);
    }

    // Strict Flag を再指定し、事前計算と同じ長さだけ書き込めた場合に限り成功とする
    const int writtenLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, a_text.data(), sourceLength,
                                                  a_output.data(), convertedLength);

    if (writtenLength != convertedLength)
    {
        const DWORD nativeCode = GetLastError();
        a_output.clear();
        return classify_native_failure(nativeCode);
    }

    return make_success();
}

/// @brief Windows UTF-16 を UTF-8 へ厳密変換する
WindowsUtfConversionResult convert_windows_utf16_to_utf8(
    std::wstring_view a_text, std::string &a_output, EmergencyHandler &a_emergencyHandler) noexcept
{
    a_output.clear();

    if (a_text.empty())
    {
        return make_success();
    }

    if (!is_windows_utf_conversion_length_supported(a_text.size()))
    {
        return make_failure(WindowsUtfConversionStatus::InputTooLong, ERROR_INSUFFICIENT_BUFFER);
    }

    // Windows wchar_t の UTF-16 Code Unit 数を明示し、埋め込み NUL を終端として扱わない
    const int sourceLength = static_cast<int>(a_text.size());
    const int convertedLength = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, a_text.data(), sourceLength,
                                                    nullptr, 0, nullptr, nullptr);

    if (convertedLength == 0)
    {
        return classify_native_failure(GetLastError());
    }

    try
    {
        a_output.resize(static_cast<std::size_t>(convertedLength));
    }
    catch (...)
    {
        terminate_allocation(a_emergencyHandler);
    }

    // 必要量取得時と同じ Strict 条件で変換し、部分的な出力を成功として返さない
    const int writtenLength = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, a_text.data(), sourceLength,
                                                  a_output.data(), convertedLength, nullptr, nullptr);

    if (writtenLength != convertedLength)
    {
        const DWORD nativeCode = GetLastError();
        a_output.clear();
        return classify_native_failure(nativeCode);
    }

    return make_success();
}
} // namespace cue
