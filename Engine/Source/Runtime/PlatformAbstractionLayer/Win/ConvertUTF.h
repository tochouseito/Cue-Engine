#pragma once

/// ********************************************************************************
/// HRESULT 変換
/// ********************************************************************************

// === Base includes ===
#include <CueResult.h>

// === C++ includes ===
#include <string>

// === win_platform includes ===
#include "WinCommon.h"
#include "ConvertHresult.h"

namespace Cue::PAL::Win
{
    /// @brief UTF-8 文字列をワイド文字列へ変換
    /// @param a_text 変換元 UTF-8 文字列
    /// @param a_outText 変換結果
    /// @return 変換結果
    static Result utf8_to_wide(std::string_view a_text, std::wstring* a_outText) noexcept
    {
        if (a_outText == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Output pointer must not be null.");
        }

        if (a_text.empty())
        {
            a_outText->clear();
            return Result::ok();
        }

        const int needed = ::MultiByteToWideChar(CP_UTF8, 0, a_text.data(), static_cast<int>(a_text.size()), nullptr, 0);
        if (needed <= 0)
        {
            return Result::fail(
                convert_hresult_code(HRESULT_FROM_WIN32(::GetLastError())), Severity::Error,
                "Failed to calculate needed buffer size for UTF-8 to wide char conversion.");
        }

        a_outText->assign(static_cast<size_t>(needed), L'\0');
        const int written = ::MultiByteToWideChar(CP_UTF8, 0, a_text.data(), static_cast<int>(a_text.size()), a_outText->data(), needed);
        if (written != needed)
        {
            return Result::fail(
                convert_hresult_code(HRESULT_FROM_WIN32(::GetLastError())), Severity::Error,
                "Failed to convert UTF-8 to wide char.");
        }

        return Result::ok();
    }

    /// @brief ワイド文字列を UTF-8 文字列へ変換
    /// @param a_text 変換元ワイド文字列
    /// @param a_outText 変換結果
    /// @return 変換結果
    static Result wide_to_utf8(std::wstring_view a_text, std::string* a_outText) noexcept
    {
        if (a_outText == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Output pointer must not be null.");
        }

        // 空文字
        if (a_text.empty())
        {
            a_outText->clear();
            return Result::ok();
        }

        // 必要サイズ
        const int needed = ::WideCharToMultiByte(CP_UTF8, 0, a_text.data(), static_cast<int>(a_text.size()), nullptr, 0, nullptr, nullptr);
        if (needed <= 0)
        {
            return Result::fail(
                convert_hresult_code(HRESULT_FROM_WIN32(::GetLastError())), Severity::Error,
                "Failed to calculate needed buffer size for wide char to UTF-8 conversion.");
        }

        // 変換
        a_outText->assign(static_cast<size_t>(needed), '\0');
        const int written = ::WideCharToMultiByte(CP_UTF8, 0, a_text.data(), static_cast<int>(a_text.size()), a_outText->data(), needed, nullptr, nullptr);
        if (written != needed)
        {
            return Result::fail(
                convert_hresult_code(HRESULT_FROM_WIN32(::GetLastError())), Severity::Error,
                "Failed to convert wide char to UTF-8.");
        }

        return Result::ok();
    }
}
