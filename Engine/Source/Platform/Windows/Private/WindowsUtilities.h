#pragma once

#include <Cue/Foundation/Error.h>
#include <Cue/Foundation/Result.h>
#include <Cue/Platform/Window.h>

#include <Windows.h>

#include <cstdint>
#include <string_view>

namespace cue
{
class AssertContext;

namespace windows_private
{
/// @brief Cue.Platform.Windows Domainの失敗をNative情報を持たない診断Errorとして生成する
[[nodiscard]] Error make_error(const AssertContext &a_context, std::int64_t a_code,
                               std::string_view a_summary) noexcept;

/// @brief Cue.Platform.Windows DomainのWin32失敗をNative Code付き診断Errorとして生成する
[[nodiscard]] Error make_native_error(const AssertContext &a_context, std::int64_t a_code,
                                      std::string_view a_summary, DWORD a_nativeCode) noexcept;

/// @brief 指定Module DomainのWin32失敗をNative Code付き診断Errorとして生成する
[[nodiscard]] Error make_native_error(const AssertContext &a_context, std::string_view a_errorDomain,
                                      std::int64_t a_code, std::string_view a_summary, DWORD a_nativeCode) noexcept;

/// @brief HWNDのClient RectangleをPlatform Window Sizeへ変換し、取得失敗を既定Errorとして返す
[[nodiscard]] Result<WindowSize> query_client_size(HWND a_window, const AssertContext &a_context) noexcept;

/// @brief HWNDのClient RectangleをPlatform Window Sizeへ変換し、取得失敗を指定Error Domainで返す
[[nodiscard]] Result<WindowSize> query_client_size(HWND a_window, const AssertContext &a_context,
                                                   std::string_view a_errorDomain, std::int64_t a_errorCode,
                                                   std::string_view a_errorSummary) noexcept;
} // namespace windows_private
} // namespace cue
