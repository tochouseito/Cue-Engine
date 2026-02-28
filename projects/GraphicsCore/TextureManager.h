#pragma once
#include "GraphicsCommon.h"

namespace Cue::GraphicsCore
{
    struct TextureDesc
    {

    };

    class TextureManager
    {
    public:
        TextureManager() = default;
        ~TextureManager() = default;

        // virtual TextureHandle create_texture(const TextureDesc& desc) = 0;
    private:
        TextureRegistry m_textureRegistry;
    };
} // namespace Cue::GraphicsCore
