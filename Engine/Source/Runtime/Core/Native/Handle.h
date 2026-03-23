#pragma once

// === C++ includes ===
#include <cstdint>
#include <string_view>

namespace Cue::Core
{
    /// @brief リソース名を整数 ID へ変換するハッシュ値です。
    using ResourceNameId = uint64_t;

    /// @brief UTF-8 文字列から FNV-1a 64bit ハッシュを計算します。
    /// @param a_text ハッシュ化する文字列です。
    /// @return 計算したハッシュ値です。
    constexpr ResourceNameId fnv1a64(std::string_view a_text) noexcept
    {
        ResourceNameId hash = 14695981039346656037ull;
        for (char currentChar : a_text)
        {
            hash ^= static_cast<unsigned char>(currentChar);
            hash *= 1099511628211ull;
        }
        return hash;
    }

    /// @brief 世代付きの汎用リソースハンドルです。
    template <class Tag>
    struct Handle final
    {
        static constexpr uint32_t k_invalid = 0xFFFFFFFFu; // 無効なハンドルを示す値

        uint32_t index = k_invalid; // リソースのインデックス
        uint32_t generation = 0; // 世代管理用のカウンタ

        /// @brief ハンドルが有効かを返します。
        /// @return インデックスが無効値でなければ `true` です。
        [[nodiscard]] bool valid() const noexcept
        {
            return index != k_invalid;
        }

        /// @brief ハンドル同士が同一かを比較します。
        /// @param a_other 比較対象のハンドルです。
        /// @return インデックスと世代が一致すれば `true` です。
        bool operator==(const Handle& a_other) const noexcept
        {
            return (index == a_other.index) && (generation == a_other.generation);
        }
    };

    // リソースハンドルのタグ型
    struct TestTag {};

    // エイリアス
    using TestHandle = Handle<TestTag>;
}
