#pragma once

// === C++ Standard Library ===
#include <string>
#include <string_view>

// 内部パスはutf-8
// 区切りは '/' 固定

namespace Cue::Core::IO
{
    class Path
    {
    public:
        Path() = default;
        explicit Path(std::string utf8) noexcept;

        [[nodiscard]] const std::string& utf8() const noexcept;

        [[nodiscard]] bool is_empty() const noexcept;
        [[nodiscard]] bool is_absolute() const noexcept; // VFS基準: "/" or "X:/"

        [[nodiscard]] Path normalize() const noexcept;

        [[nodiscard]] Path parent() const noexcept;
        [[nodiscard]] std::string filename() const noexcept;
        [[nodiscard]] std::string stem() const noexcept;
        [[nodiscard]] std::string extension() const noexcept;

        [[nodiscard]] static Path join(const Path& a, const Path& b) noexcept;

    private:
        std::string m_utf8;
    };
}
