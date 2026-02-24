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

        Result create_texture(const TextureDesc& desc)
        {

        }

        const TextureRecord& get_texture(TextureHandle handle) const noexcept
        {

        }
    private:
        TextureRegistry m_textureRegistry;
    };
}
