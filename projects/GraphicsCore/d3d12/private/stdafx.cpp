#include "stdafx.h"

namespace Cue::Graphics::DX12
{
    std::string to_utf8(std::wstring_view w) noexcept
    {
        // 1) 空対策
        if (w.empty())
        {
            return {};
        }

        // 2) 必要長取得
        const int len = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
        if (len <= 0)
        {
            return {};
        }

        // 3) 変換
        std::string s;
        s.resize(static_cast<size_t>(len));
        ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), len, nullptr, nullptr);

        return s;
    }
    std::wstring to_utf16(std::string_view s) noexcept
    {
        // 1) 空対策
        if (s.empty())
        {
            return {};
        }

        // 2) 必要長取得
        const int len = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        if (len <= 0)
        {
            return {};
        }

        // 3) 変換
        std::wstring w;
        w.resize(static_cast<size_t>(len));
        ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), len);

        return w;
    }
}
