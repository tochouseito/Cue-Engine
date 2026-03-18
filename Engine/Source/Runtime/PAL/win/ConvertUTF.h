#pragma once

// === Base includes ===
#include <Result.h>

// === C++ includes ===
#include <string>

// === Windows API include ===
#include "stdafx.h"
#include "ConvertHresult.h"

namespace Cue::PAL::Win
{
    [[nodiscard]] static Result utf8_to_wide(std::string_view s, std::wstring* out) noexcept
    {
        // 1) 引数チェック
        if (out == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Output pointer must not be null.");
        }

        // 2) 空文字
        if (s.empty())
        {
            out->clear();
            return Result::ok();
        }

        // 3) 必要サイズ
        const int needed = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        if (needed <= 0)
        {
            return Result::fail(
                convert_hresult_code(HRESULT_FROM_WIN32(::GetLastError())), Severity::Error,
                "Failed to calculate needed buffer size for UTF-8 to wide char conversion.");
        }

        // 4) 変換
        out->assign(static_cast<size_t>(needed), L'\0');
        const int written = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out->data(), needed);
        if (written != needed)
        {
            return Result::fail(
                convert_hresult_code(HRESULT_FROM_WIN32(::GetLastError())), Severity::Error,
                "Failed to convert UTF-8 to wide char.");
        }

        return Result::ok();
    }

    [[nodiscard]] static Result wide_to_utf8(std::wstring_view s, std::string* out) noexcept
    {
        // 1) 引数チェック
        if (out == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Output pointer must not be null.");
        }

        // 2) 空文字
        if (s.empty())
        {
            out->clear();
            return Result::ok();
        }

        // 3) 必要サイズ
        const int needed = ::WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
        if (needed <= 0)
        {
            return Result::fail(
                convert_hresult_code(HRESULT_FROM_WIN32(::GetLastError())), Severity::Error,
                "Failed to calculate needed buffer size for wide char to UTF-8 conversion.");
        }

        // 4) 変換
        out->assign(static_cast<size_t>(needed), '\0');
        const int written = ::WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out->data(), needed, nullptr, nullptr);
        if (written != needed)
        {
            return Result::fail(
                convert_hresult_code(HRESULT_FROM_WIN32(::GetLastError())), Severity::Error,
                "Failed to convert wide char to UTF-8.");
        }

        return Result::ok();
    }
}
