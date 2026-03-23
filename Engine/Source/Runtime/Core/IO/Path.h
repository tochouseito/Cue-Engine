#pragma once

// === C++ includes ===
#include <string>
#include <string_view>

// 内部パスは utf-8
// 区切りは '/' 固定

namespace Cue::Core::IO
{
    /// @brief UTF-8 ベースの仮想パスです。
    class Path
    {
    public:
        Path() = default;
        /// @brief UTF-8 文字列からパスを構築します。
        /// @param a_utf8 保持するパス文字列です。
        explicit Path(std::string a_utf8) noexcept;

        /// @brief 保持している UTF-8 文字列を返します。
        /// @return 内部表現の文字列参照です。
        [[nodiscard]] const std::string& utf8() const noexcept;

        /// @brief パスが空かを返します。
        /// @return 空文字列なら `true` です。
        [[nodiscard]] bool is_empty() const noexcept;
        /// @brief パスが絶対パスかを返します。
        /// @return VFS 基準で絶対パスなら `true` です。
        [[nodiscard]] bool is_absolute() const noexcept; // VFS 基準: "/" または "x:/"

        /// @brief パス区切りと `.` `..` を正規化します。
        /// @return 正規化済みのパスです。
        [[nodiscard]] Path normalize() const noexcept;

        /// @brief 親パスを返します。
        /// @return 正規化後の親パスです。
        [[nodiscard]] Path parent() const noexcept;
        /// @brief ファイル名部分を返します。
        /// @return 末尾の要素名です。
        [[nodiscard]] std::string filename() const noexcept;
        /// @brief 拡張子を除いたファイル名を返します。
        /// @return ステム文字列です。
        [[nodiscard]] std::string stem() const noexcept;
        /// @brief 拡張子を返します。
        /// @return 先頭に `.` を含む拡張子です。
        [[nodiscard]] std::string extension() const noexcept;

        /// @brief 2 つのパスを結合します。
        /// @param a_left ベース側のパスです。
        /// @param a_right 連結するパスです。
        /// @return 正規化済みの結合結果です。
        [[nodiscard]] static Path join(const Path& a_left, const Path& a_right) noexcept;

    private:
        std::string m_utf8;
    };
} // 名前空間 cue::core::io
