#pragma once

/// ************************************************************************************
/// テクスチャマネージャーインタフェース
/// ************************************************************************************

// === RHI Includes ===
#include "RHICommon.h"

// === C++ includes ===
#include <cstddef>
#include <span>

namespace Cue::RHI
{
    enum class TextureType : uint8_t
    {
        Texture2D,
        Texture3D,
        CubeMap
    };

    enum class TextureKind : uint8_t
    {
        Default, // GPU 専用のヒープに配置されるテクスチャ
        RenderTarget, // レンダーターゲットとして使用されるテクスチャ
        DepthStencil, // 深度ステンシルバッファとして使用されるテクスチャ
    };

    struct TextureDesc
    {
        std::string name;
        uint32_t bufferCount = 1;
        TextureType type = TextureType::Texture2D;
        TextureKind kind = TextureKind::Default;
        uint32_t width = 0;
        uint32_t height = 0;
        uint16_t mipLevels = 1;
        uint16_t arraySize = 1; // 配列テクスチャの場合の配列サイズ
        ColorFormat format = ColorFormat::R8G8B8A8_UNORM;
        uint32_t sampleCount = 1; // マルチサンプルのサンプル数
        bool allowUnorderedAccess = false;
        float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f }; // レンダーターゲット用のクリアカラー
        float clearDepth = 1.0f; // 深度ステンシル用のクリア深度
        uint8_t clearStencil = 0; // 深度ステンシル用のクリアステンシル値
    };

    struct TextureSubresourceData final
    {
        const std::byte* data = nullptr;
        uint64_t dataSize = 0;
        uint32_t rowPitch = 0;
        uint32_t slicePitch = 0;
    };

    class ITextureManager
    {
    public:
        ITextureManager() = default;
        // コピー禁止
        ITextureManager(const ITextureManager&) = delete;
        ITextureManager& operator=(const ITextureManager&) = delete;
        // ムーブ禁止
        ITextureManager(ITextureManager&&) = delete;
        ITextureManager& operator=(ITextureManager&&) = delete;
        virtual ~ITextureManager() = default;

        // --- テクスチャの生成と破棄 ---
        virtual Result create_texture(const TextureDesc& desc, TextureHandle& out) = 0;
        virtual Result create_texture(const TextureDesc& desc,
            std::span<const TextureSubresourceData> initialData,
            TextureHandle& out) = 0;
        virtual Result create_texture_from_file(
            std::string_view name,
            std::string_view filePath,
            TextureHandle& out)
        {
            (void)name;
            (void)filePath;
            out = {};
            return Result::fail(
                Code::Unsupported,
                Severity::Error,
                "Texture file loading is not supported by this backend.");
        }
        virtual Result destroy_texture(TextureHandle handle) = 0;
        virtual Result get_texture_descriptor_index(TextureHandle handle,
            uint32_t& outIndex) = 0;
        virtual Result get_texture_desc(TextureHandle handle, TextureDesc& outDesc)
        {
            (void)handle;
            outDesc = {};
            return Result::fail(
                Code::Unsupported,
                Severity::Error,
                "Texture description query is not supported by this backend.");
        }

        // --- 名前からテクスチャハンドルの取得 ---
        virtual Result get_texture(std::string_view name, TextureHandle& out) = 0;
    };
}
