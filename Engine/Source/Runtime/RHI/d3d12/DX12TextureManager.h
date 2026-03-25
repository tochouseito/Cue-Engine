#pragma once

// === RHI Includes ===
#include <TextureManager.h>

namespace Cue::RHI::DX12
{
    class DX12TextureManager final : public ITextureManager
    {
    public:
        DX12TextureManager() = default;
        ~DX12TextureManager() override = default;
        Result create_texture(const TextureDesc& desc, TextureHandle& out) override;
        Result destroy_texture(TextureHandle handle) override;
    };
}
