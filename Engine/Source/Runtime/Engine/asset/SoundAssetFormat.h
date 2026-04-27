#pragma once

// === C++ Includes ===
#include <cstdint>

namespace Cue
{
    struct CueSoundHeader final
    {
        uint32_t magic = 0;
        uint32_t version = 0;
        uint16_t formatTag = 1;
        uint16_t channelCount = 2;
        uint32_t samplesPerSecond = 44100;
        uint32_t averageBytesPerSecond = 0;
        uint16_t blockAlign = 0;
        uint16_t bitsPerSample = 16;
        uint32_t extraDataSize = 0;
        uint32_t flags = 0;
        uint64_t audioDataSize = 0;
    };

    inline constexpr uint32_t k_cueSoundMagic = 0x53455543u;
    inline constexpr uint32_t k_cueSoundVersion = 1u;
}
