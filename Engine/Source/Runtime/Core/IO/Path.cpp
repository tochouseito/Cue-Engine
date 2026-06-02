// === Core includes ===
#include "Core_pch.h"
#include "Path.h"

// === C++ includes ===
#include <cctype>
#include <vector>

namespace
{
    [[nodiscard]] static bool is_sep(const char a_char) noexcept
    {
        return (a_char == '/') || (a_char == '\\');
    }

    [[nodiscard]] static bool is_alpha_ascii(const char a_char) noexcept
    {
        const unsigned char uc = static_cast<unsigned char>(a_char);
        return (uc >= 'A' && uc <= 'Z') || (uc >= 'a' && uc <= 'z');
    }

    struct Prefix
    {
        std::string text;     // "", "C:"
        bool has_root_slash;  // "/" が付いているか（例: "/a", "C:/a", "//server"）
        bool is_unc;          // "//" 始まり
    };

    [[nodiscard]] static Prefix parse_prefix_and_sanitize(std::string& a_text) noexcept
    {
        // - 区切りを "/" に統一
        for (char& c : a_text)
        {
            if (c == '\\')
            {
                c = '/';
            }
        }

        // - prefix 判定
        Prefix p{};
        if (a_text.size() >= 2 && is_alpha_ascii(a_text[0]) && a_text[1] == ':')
        {
            p.text.push_back(a_text[0]);
            p.text.push_back(':');

            // ドライブレター + '/' 形式のみ absolute として扱う
            if (a_text.size() >= 3 && a_text[2] == '/')
            {
                p.has_root_slash = true;
            }
            return p;
        }

        if (a_text.size() >= 2 && a_text[0] == '/' && a_text[1] == '/')
        {
            p.is_unc = true;
            p.has_root_slash = true;
            return p;
        }

        if (!a_text.empty() && a_text[0] == '/')
        {
            p.has_root_slash = true;
            return p;
        }

        return p;
    }

    [[nodiscard]] static std::string build_normalized(const Prefix& a_prefix, const std::vector<std::string_view>& a_parts) noexcept
    {
        // - 先頭（prefix + root）
        std::string out{};
        out.reserve(64);

        if (!a_prefix.text.empty())
        {
            out += a_prefix.text;
        }

        if (a_prefix.is_unc)
        {
            out += "//";
        }
        else if (a_prefix.has_root_slash)
        {
            out += "/";
        }

        // - パーツ結合
        for (size_t i = 0; i < a_parts.size(); ++i)
        {
            if (!out.empty() && out.back() != '/' && out.back() != ':')
            {
                out += "/";
            }
            out.append(a_parts[i].data(), a_parts[i].size());
        }

        // - 空なら "." ではなく "" を返す（VFSキーとして扱いやすい）
        return out;
    }

    [[nodiscard]] static std::string_view skip_prefix_view(std::string_view a_text, const Prefix& a_prefix) noexcept
    {
        // - "C:" をスキップ
        if (!a_prefix.text.empty())
        {
            if (a_text.size() >= 2)
            {
                a_text.remove_prefix(2);
            }
        }

        // - UNC/絶対の "/" をスキップ（先頭の区切りは正規化で処理）
        if (a_prefix.is_unc)
        {
            if (a_text.size() >= 2)
            {
                a_text.remove_prefix(2); // "//"
            }
        }
        else if (a_prefix.has_root_slash)
        {
            if (!a_text.empty() && a_text[0] == '/')
            {
                a_text.remove_prefix(1);
            }
        }

        return a_text;
    }
}

namespace Cue::Core::IO
{
    Path::Path(std::string a_utf8) noexcept
        : m_utf8(std::move(a_utf8))
    {}

    const std::string& Path::utf8() const noexcept
    {
        return m_utf8;
    }

    bool Path::is_empty() const noexcept
    {
        return m_utf8.empty();
    }

    bool Path::is_absolute() const noexcept
    {
        // - normalize 前提ではなく、生文字列から判定する（保守的）
        if (m_utf8.size() >= 1 && (m_utf8[0] == '/' || m_utf8[0] == '\\'))
        {
            return true;
        }
        if (m_utf8.size() >= 3 && is_alpha_ascii(m_utf8[0]) && m_utf8[1] == ':' && is_sep(m_utf8[2]))
        {
            return true;
        }
        return false;
    }

    Path Path::normalize() const noexcept
    {
        // - 入力コピー（sanitizeで書き換える）
        std::string s = m_utf8;

        // - prefix 解析 + 区切り統一
        const Prefix p = parse_prefix_and_sanitize(s);

        // - prefix 部分を除いた view を作成
        std::string_view body = skip_prefix_view(std::string_view{ s }, p);

        // - パーツを走査して '.' と '..' を字句的に解決
        std::vector<std::string_view> stack{};
        stack.reserve(16);

        size_t i = 0;
        while (i < body.size())
        {
            // 4-1) 連続 "/" をスキップ
            while (i < body.size() && body[i] == '/')
            {
                ++i;
            }
            if (i >= body.size())
            {
                break;
            }

            // 4-2) token 取り出し
            const size_t start = i;
            while (i < body.size() && body[i] != '/')
            {
                ++i;
            }
            const size_t len = i - start;
            const std::string_view token = body.substr(start, len);

            // 4-3) '.' / '..' 解決
            if (token == ".")
            {
                continue;
            }
            if (token == "..")
            {
                if (!stack.empty() && stack.back() != "..")
                {
                    stack.pop_back();
                    continue;
                }

                // absolute の場合は root より上に行かせない
                if (p.has_root_slash || p.is_unc)
                {
                    continue;
                }

                // relative の場合は保持
                stack.push_back(token);
                continue;
            }

            // 4-4) 通常トークン
            stack.push_back(token);
        }

        // - 組み立て
        return Path{ build_normalized(p, stack) };
    }

    Path Path::join(const Path& a_left, const Path& a_right) noexcept
    {
        // - a_right が絶対なら a_right を優先
        if (a_right.is_absolute())
        {
            return a_right.normalize();
        }

        // - a_left が空なら a_right
        if (a_left.m_utf8.empty())
        {
            return a_right.normalize();
        }

        // - "/" を挟んで結合（normalize が整える）
        std::string out = a_left.m_utf8;
        if (!out.empty() && !is_sep(out.back()))
        {
            out.push_back('/');
        }
        out += a_right.m_utf8;

        return Path{ std::move(out) }.normalize();
    }

    Path Path::parent() const noexcept
    {
        // - normalize 済みにしてから処理（区切りと ".." を消す）
        const Path n = normalize();
        const std::string& s = n.m_utf8;

        // - 空またはルートはそのまま
        if (s.empty())
        {
            return Path{};
        }
        if (s == "/" || (s.size() == 3 && is_alpha_ascii(s[0]) && s[1] == ':' && s[2] == '/'))
        {
            return n;
        }

        // - 末尾 "/" は無視（ただしルートは除外）
        size_t end = s.size();
        while (end > 0 && s[end - 1] == '/')
        {
            --end;
        }

        // - 最後の "/" を探す
        const size_t pos = s.rfind('/', end - 1);
        if (pos == std::string::npos)
        {
            return Path{};
        }

        // - "C:/" のような root を壊さない
        if (pos == 2 && s.size() >= 3 && is_alpha_ascii(s[0]) && s[1] == ':' && s[2] == '/')
        {
            return Path{ s.substr(0, 3) };
        }

        if (pos == 0)
        {
            return Path{ "/" };
        }

        return Path{ s.substr(0, pos) };
    }

    std::string Path::filename() const noexcept
    {
        // - normalize 済みから取得
        const Path n = normalize();
        const std::string& s = n.m_utf8;
        if (s.empty() || s == "/")
        {
            return {};
        }

        // - 末尾 "/" を除外
        size_t end = s.size();
        while (end > 0 && s[end - 1] == '/')
        {
            --end;
        }
        if (end == 0)
        {
            return {};
        }

        // - 最後の "/" の次
        const size_t pos = s.rfind('/', end - 1);
        if (pos == std::string::npos)
        {
            return s.substr(0, end);
        }
        return s.substr(pos + 1, end - (pos + 1));
    }

    std::string Path::extension() const noexcept
    {
        // - filename から拡張子を取る（.bashrc みたいなのは拡張子なし扱い）
        const std::string name = filename();
        if (name.empty())
        {
            return {};
        }

        // - 最後の '.' を探す
        const size_t dot = name.rfind('.');
        if (dot == std::string::npos || dot == 0 || dot == name.size() - 1)
        {
            return {};
        }

        return name.substr(dot);
    }

    std::string Path::stem() const noexcept
    {
        // - filename から拡張子を除く
        const std::string name = filename();
        if (name.empty())
        {
            return {};
        }

        // - 最後の '.' を探す
        const size_t dot = name.rfind('.');
        if (dot == std::string::npos || dot == 0)
        {
            return name;
        }

        return name.substr(0, dot);
    }
}
