#include "WindowsUtilities.h"

#include <Cue/Foundation/Assert.h>

#include <utility>

namespace
{
constexpr std::int64_t k_clientSizeQueryFailed = 7;
}

namespace cue::windows_private
{
/// @brief 既定Platform Windows Domainを指定する共通Error生成Overloadへ転送する
Error make_error(const AssertContext &a_context, std::int64_t a_code, std::string_view a_summary) noexcept
{
    ErrorCode code = ErrorCode::create(a_context.fatal_handler(), "Cue.Platform.Windows", a_code);
    return Error::create(a_context.fatal_handler(), std::move(code), a_summary);
}

/// @brief 既定Platform Windows Domainを指定する共通Native Error生成Overloadへ転送する
Error make_native_error(const AssertContext &a_context, std::int64_t a_code, std::string_view a_summary,
                        DWORD a_nativeCode) noexcept
{
    return make_native_error(a_context, "Cue.Platform.Windows", a_code, a_summary, a_nativeCode);
}

/// @brief 指定Module DomainとWin32 Codeを失わず診断Errorへ格納する
Error make_native_error(const AssertContext &a_context, std::string_view a_errorDomain, std::int64_t a_code,
                        std::string_view a_summary, DWORD a_nativeCode) noexcept
{
    ErrorCode code = ErrorCode::create(a_context.fatal_handler(), a_errorDomain, a_code);
    NativeError nativeError = NativeError::create(a_context.fatal_handler(), "Win32", a_nativeCode);
    return Error::create(a_context.fatal_handler(), std::move(code), a_summary, std::move(nativeError));
}

/// @brief Client Size取得失敗に既定Platform Windows Error identityを与えるOverloadへ転送する
Result<WindowSize> query_client_size(HWND a_window, const AssertContext &a_context) noexcept
{
    return query_client_size(a_window, a_context, "Cue.Platform.Windows", k_clientSizeQueryFailed,
                             "Windows Window client size query failed");
}

/// @brief GetClientRectの結果を所有値のWindow Sizeへ変換し、失敗時は指定Error identityを保持する
Result<WindowSize> query_client_size(HWND a_window, const AssertContext &a_context,
                                     std::string_view a_errorDomain, std::int64_t a_errorCode,
                                     std::string_view a_errorSummary) noexcept
{
    RECT clientRectangle = {};

    if (GetClientRect(a_window, &clientRectangle) == FALSE)
    {
        return Result<WindowSize>::failure(
            make_native_error(a_context, a_errorDomain, a_errorCode, a_errorSummary, GetLastError()));
    }

    WindowSize size = {
        static_cast<std::uint32_t>(clientRectangle.right - clientRectangle.left),
        static_cast<std::uint32_t>(clientRectangle.bottom - clientRectangle.top),
    };
    return Result<WindowSize>::success(std::move(size));
}
} // namespace cue::windows_private
