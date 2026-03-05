#pragma once
#include <cstdint>
#include <string_view>

namespace Cue::GraphicsCore
{
    // 文字列を整数へ変換するハッシュ関数
    using ResourceNameId = uint64_t;
    constexpr ResourceNameId fnv1a64(std::string_view text) noexcept
    {
        ResourceNameId hash = 14695981039346656037ull;
        for (char c : text)
        {
            hash ^= static_cast<unsigned char>(c);
            hash *= 1099511628211ull;
        }
        return hash;
    }

    // 汎用リソースハンドル
    template <class Tag>
    struct Handle final
    {
        static constexpr uint32_t k_invalid = 0xFFFFFFFFu; // 無効なハンドルを示す値

        uint32_t index = k_invalid; // リソースのインデックス
        uint32_t generation = 0; // 世代管理用のカウンタ

        // ハンドルが有効かどうかをチェックする関数
        [[nodiscard]] bool valid() const noexcept
        {
            return index != k_invalid;
        }

        // ハンドル同士の比較演算子
        bool operator==(const Handle& other) const noexcept
        {
            return (index == other.index) && (generation == other.generation);
        }
    };

    // リソースハンドルのタグ型
    struct BufferTag {};
    struct TextureTag {};
    struct PipelineStateTag {};
    struct RootSignatureTag {};
    struct ShaderBlobTag {};
    struct ViewTag {};

    // エイリアス
    using BufferHandle = Handle<BufferTag>;
    using TextureHandle = Handle<TextureTag>;
    using PipelineStateHandle = Handle<PipelineStateTag>;
    using RootSignatureHandle = Handle<RootSignatureTag>;
    using ShaderBlobHandle = Handle<ShaderBlobTag>;
    using ViewHandle = Handle<ViewTag>;
}// namespace Cue::GraphicsCore
