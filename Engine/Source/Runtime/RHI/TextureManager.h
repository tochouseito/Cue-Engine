#pragma once

// === RHI Includes ===
#include "RHICommon.h"

namespace Cue::RHI
{
    enum class TextureType : uint8_t
    {
        Texture2D,
        RenderTarget,
        DepthStencil
    };

    struct TextureDesc
    {
        std::string_view name = {};
        TextureType type = TextureType::Texture2D;
        uint32_t defaultHeapCount = 1;
        uint32_t uploadHeapCount = 0;
        ResourceState initialState = ResourceState::Common;
        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t depthOrArraySize = 1;
        uint32_t mipLevels = 1;
        ColorFormat colorFormat = ColorFormat::R8G8B8A8_UNORM;
        DSVFormat dsvFormat = DSVFormat::D24_UNorm_S8_UInt;
        bool allowUnorderedAccess = false;
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
        virtual Result destroy_texture(TextureHandle handle) = 0;
    };
}
