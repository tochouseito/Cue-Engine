#pragma once

/// *********************************************************************************
/// UTF-8 のファイルパス
/// *********************************************************************************

// 内部パスは utf-8
// 区切りは '/' 固定

// === C++ includes ===
#include <string>
#include <string_view>

namespace Cue::Core::IO
{
    /// @brief UTF-8 ベースの仮想パス
    class Path
    {
    public:
        Path() = default;
        /// @brief UTF-8 文字列からパスを構築
        /// @param a_utf8 保持するパス文字列
        explicit Path(std::string a_utf8) noexcept;

        /// @brief char* 形式のパスを構築
        /// @param a_utf8 保持するパス文字列
        explicit Path(const char* a_utf8) noexcept;

        /// @brief 保持している UTF-8 文字列を返す
        /// @return 内部表現の文字列参照
        [[nodiscard]] const std::string& utf8() const noexcept;

        /// @brief パスが空かを返す
        /// @return 空文字列なら `true`
        [[nodiscard]] bool is_empty() const noexcept;
        /// @brief パスが絶対パスかを返す
        /// @return VFS 基準で絶対パスなら `true`
        [[nodiscard]] bool is_absolute() const noexcept; // VFS 基準: "/" または "x:/"

        /// @brief パス区切りと `.` `..` を正規化
        /// @return 正規化済みのパス
        [[nodiscard]] Path normalize() const noexcept;

        /// @brief 親パスを返す
        /// @return 正規化後の親パス
        [[nodiscard]] Path parent() const noexcept;
        /// @brief ファイル名部分を返す
        /// @return 末尾の要素名
        [[nodiscard]] std::string filename() const noexcept;
        /// @brief 拡張子を除いたファイル名を返す
        /// @return ステム文字列
        [[nodiscard]] std::string stem() const noexcept;
        /// @brief 拡張子を返す
        /// @return 先頭に `.` を含む拡張子
        [[nodiscard]] std::string extension() const noexcept;

        /// @brief 2 つのパスを結合する
        /// @param a_left ベース側のパス
        /// @param a_right 連結するパス
        /// @return 正規化済みの結合結果
        [[nodiscard]] static Path join(const Path& a_left, const Path& a_right) noexcept;

    private:
        std::string m_utf8;
    };
} // namespace Cue::Core::IO
