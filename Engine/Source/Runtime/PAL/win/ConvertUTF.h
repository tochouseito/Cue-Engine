#pragma once

// === Base includes ===
#include <Result.h>

// === C++ includes ===
#include <string>

// === Windows API includes ===
#include "ConvertHresult.h"
#include "stdafx.h"

namespace Cue::PAL::Win
{
    static Result utf8_to_wide(std::string_view a_text, std::wstring* a_outText) noexcept
    {
        // 1) 引数チェック
        if (a_outText == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Output pointer must not be null.");
        }

        // 2) 空文字
        if (a_text.empty())
        {
            a_outText->clear();
            return Result::ok();
        }

        // 3) 必要サイズ
        const int needed = ::MultiByteToWideChar(CP_UTF8, 0, a_text.data(), static_cast<int>(a_text.size()), nullptr, 0);
        if (needed <= 0)
        {
            return Result::fail(
                convert_hresult_code(HRESULT_FROM_WIN32(::GetLastError())), Severity::Error,
                "Failed to calculate needed buffer size for UTF-8 to wide char conversion.");
        }

        // 4) 変換
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

    static Result wide_to_utf8(std::wstring_view a_text, std::string* a_outText) noexcept
    {
        // 1) 引数チェック
        if (a_outText == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Output pointer must not be null.");
        }

        // 2) 空文字
        if (a_text.empty())
        {
            a_outText->clear();
            return Result::ok();
        }

        // 3) 必要サイズ
        const int needed = ::WideCharToMultiByte(CP_UTF8, 0, a_text.data(), static_cast<int>(a_text.size()), nullptr, 0, nullptr, nullptr);
        if (needed <= 0)
        {
            return Result::fail(
                convert_hresult_code(HRESULT_FROM_WIN32(::GetLastError())), Severity::Error,
                "Failed to calculate needed buffer size for wide char to UTF-8 conversion.");
        }

        // 4) 変換
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
