#include "UtfConversion.h"

#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Error.h>
#include <Cue/Platform/Windows/WindowsPlatform.h>

#include <Windows.h>

#include <cstdlib>
#include <limits>
#include <utility>

namespace
{
constexpr std::int64_t k_invalidUtf8 = 2;
constexpr std::int64_t k_invalidUtf16 = 11;

/// @brief Allocation 失敗経路が追加 Allocation なしで Process を終了することを検証する
[[noreturn]] void terminate_allocation(const cue::AssertContext &a_context) noexcept
{
    a_context.fatal_handler().terminate("UTF conversion allocation failed");
    std::abort();
}

/// @brief Windows UTF 変換境界で使用する Conversion Error を生成し、呼び出し元へ返す
[[nodiscard]] cue::Error make_conversion_error(const cue::AssertContext &a_context, std::int64_t a_code,
                                               std::string_view a_summary, DWORD a_nativeCode) noexcept
{
    cue::ErrorCode code = cue::ErrorCode::create(a_context.fatal_handler(), "Cue.Platform.Windows", a_code);
    cue::NativeError nativeError = cue::NativeError::create(a_context.fatal_handler(), "Win32", a_nativeCode);
    return cue::Error::create(a_context.fatal_handler(), std::move(code), a_summary, std::move(nativeError));
}
} // namespace

namespace cue
{
Result<std::wstring> utf8_to_utf16(std::string_view a_text, const AssertContext &a_assertContext) noexcept
{
    if (a_text.empty())
    {
        return Result<std::wstring>::success(std::wstring());
    }

    if (a_text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return Result<std::wstring>::failure(
            make_conversion_error(a_assertContext, k_invalidUtf8, "UTF-8 title is too long",
                                  ERROR_INSUFFICIENT_BUFFER));
    }

    // Win32 の長さ引数へ安全に収めた後、終端文字に依存せず String View 全体を変換する
    int sourceLength = static_cast<int>(a_text.size());
    // 必要量を先に問い合わせ、変換結果を一回の Allocation で所有文字列へ格納する
    int convertedLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, a_text.data(), sourceLength, nullptr, 0);

    if (convertedLength == 0)
    {
        DWORD nativeCode = GetLastError();
        return Result<std::wstring>::failure(
            make_conversion_error(a_assertContext, k_invalidUtf8, "UTF-8 title is invalid", nativeCode));
    }

    try
    {
        std::wstring result(static_cast<std::size_t>(convertedLength), L'\0');
        // 不正な UTF-8 を置換せず Error にして、Window Title の文字化けを診断可能にする
        int writtenLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, a_text.data(), sourceLength,
                                                result.data(), convertedLength);

        if (writtenLength != convertedLength)
        {
            DWORD nativeCode = GetLastError();
            return Result<std::wstring>::failure(
                make_conversion_error(a_assertContext, k_invalidUtf8, "UTF-8 title conversion failed", nativeCode));
        }

        return Result<std::wstring>::success(std::move(result));
    }
    catch (...)
    {
        terminate_allocation(a_assertContext);
    }
}

Result<std::string> convert_windows_argument_to_utf8(std::wstring_view a_text,
                                                     const AssertContext &a_assertContext) noexcept
{
    if (a_text.empty())
    {
        return Result<std::string>::success(std::string());
    }

    if (a_text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return Result<std::string>::failure(
            make_conversion_error(a_assertContext, k_invalidUtf16, "UTF-16 argument is too long",
                                  ERROR_INSUFFICIENT_BUFFER));
    }

    // Win32 Entry Point の UTF-16 を共通 Runtime が扱う UTF-8 へ正規化する
    int sourceLength = static_cast<int>(a_text.size());
    // 必要 Byte 数を先に確定し、変換途中の Buffer を公開しない
    int convertedLength = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, a_text.data(), sourceLength, nullptr, 0,
                                              nullptr, nullptr);

    if (convertedLength == 0)
    {
        DWORD nativeCode = GetLastError();
        return Result<std::string>::failure(
            make_conversion_error(a_assertContext, k_invalidUtf16, "UTF-16 argument is invalid", nativeCode));
    }

    try
    {
        std::string result(static_cast<std::size_t>(convertedLength), '\0');
        // 不正な UTF-16 を代替文字へ変えず Error にして、Runtime へ曖昧な引数を渡さない
        int writtenLength = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, a_text.data(), sourceLength,
                                                result.data(), convertedLength, nullptr, nullptr);

        if (writtenLength != convertedLength)
        {
            DWORD nativeCode = GetLastError();
            return Result<std::string>::failure(
                make_conversion_error(a_assertContext, k_invalidUtf16, "UTF-16 argument conversion failed",
                                      nativeCode));
        }

        return Result<std::string>::success(std::move(result));
    }
    catch (...)
    {
        terminate_allocation(a_assertContext);
    }
}
} // namespace cue
