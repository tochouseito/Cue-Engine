#pragma once
#include "GraphicsCommon.h"

namespace Cue::GraphicsCore
{
    enum class TextureUsage : uint8_t
    {
        Unknown,
        RenderTarget,
        DepthStencil,
    };

    struct TextureDesc
    {
        std::string_view name;
        // 0 means "use the FrameGraph default buffering count".
        uint32_t bufferingCount = 0;
        ResourceInstanceSource instanceSource = ResourceInstanceSource::FrameResourceIndex;
        TextureUsage usage = TextureUsage::Unknown;
        ResourceState initialState = ResourceState::Common;
        uint32_t width = 0;
        uint32_t height = 0;
        ColorFormat colorFormat = ColorFormat::R8G8B8A8_UNORM;
        float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        DSVFormat dsvFormat = DSVFormat::D24_UNorm_S8_UInt;
        float clearDepth = 1.0f;
        uint8_t clearStencil = 0;
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
