#pragma once

// === C++ includes ===
#include <cstdint>

namespace Cue::GpuData
{
    struct DebugPickState final
    {
        bool isRequested = false;
        bool isInFlight = false;
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t readbackResourceIndex = 0;
        uint32_t framesUntilReadable = 0;
    };
}
