#pragma once

// === C++ includes ===
#include <cstdint>

// === Math includes ===
#include <CueMath.h>

namespace Cue::GpuData
{
    struct DebugSelectionGpu final
    {
        Math::float4x4 world;
        Math::float4 color = Math::float4(1.0f, 0.84f, 0.18f, 1.0f);
        uint32_t isEnabled = 0;
        uint32_t padding0 = 0;
        uint32_t padding1 = 0;
        uint32_t padding2 = 0;
    };
}
