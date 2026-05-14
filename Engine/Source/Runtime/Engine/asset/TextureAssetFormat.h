#pragma once

// === RHI Includes ===
#include <TextureManager.h>

// === C++ Includes ===
#include <cstdint>

namespace Cue
{
    struct CueTextureHeader final
    {
        uint32_t magic = 0;
        uint32_t version = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t mipCount = 0;
        uint32_t arraySize = 0;
        uint32_t format = 0;
        uint32_t flags = 0;
        uint64_t dataSize = 0;
    };

    struct CueTextureMipInfo final
    {
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t rowPitch = 0;
        uint32_t slicePitch = 0;
        uint64_t offset = 0;
        uint64_t size = 0;
    };

    inline constexpr uint32_t k_cueTextureMagic = 0x54455543u;
    inline constexpr uint32_t k_cueTextureVersion = 1u;
    inline constexpr uint32_t k_cueTextureFlagSrgb = 0x1u;
    inline constexpr uint32_t k_cueTextureFlagCubeMap = 0x2u;
}
