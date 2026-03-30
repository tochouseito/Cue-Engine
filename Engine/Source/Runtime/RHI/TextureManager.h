#pragma once

// === RHI Includes ===
#include "RHICommon.h"

namespace Cue::RHI
{
    struct TextureDesc
    {
        std::string_view name;
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
