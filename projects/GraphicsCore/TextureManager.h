#pragma once
#include "GraphicsCommon.h"
#include "ResourceHandle.h"
#include "Registry.h"

namespace Cue::GraphicsCore
{
    struct TextureDesc
    {
        std::string_view name;
    };

    class ITextureManager
    {
    public:
        ITextureManager() = default;
        virtual ~ITextureManager() = default;
        virtual Result create_texture(const TextureDesc& desc, TextureHandle& outHandle) = 0;
        virtual Result destroy_texture(const TextureHandle& handle) = 0;
        virtual Result get_texture(ResourceNameId nameId, TextureHandle& outHandle) = 0;
    };
} // namespace Cue::GraphicsCore
