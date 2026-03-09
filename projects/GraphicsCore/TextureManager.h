#pragma once
#include "GraphicsCommon.h"

namespace Cue::GraphicsCore
{
    struct TextureDesc
    {
        std::string_view name;
        // 0 means "use the FrameGraph default buffering count".
        uint32_t bufferingCount = 0;
        ResourceInstanceSource instanceSource = ResourceInstanceSource::FrameResourceIndex;
    };

    class ITextureManager
    {
    public:
        ITextureManager() = default;
        virtual ~ITextureManager() = default;
        virtual Result create_texture(const TextureDesc& desc, TextureHandle& outHandle) = 0;
        virtual Result destroy_texture(const TextureHandle& handle) = 0;
        virtual Result get_texture(ResourceNameId nameId, uint32_t textureIndex, TextureHandle& outHandle) = 0;
        virtual Result get_texture_instance_count(ResourceNameId nameId, uint32_t& outCount) = 0;
    };
} // namespace Cue::GraphicsCore
