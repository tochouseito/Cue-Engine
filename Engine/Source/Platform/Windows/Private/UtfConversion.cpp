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
constexpr std::int64_t k_invalidUtf16 = 3;

[[noreturn]] void terminate_allocation(const cue::AssertContext &a_context) noexcept
{
    a_context.fatal_handler().terminate("UTF conversion allocation failed");
    std::abort();
}

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

    int sourceLength = static_cast<int>(a_text.size());
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

    int sourceLength = static_cast<int>(a_text.size());
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
