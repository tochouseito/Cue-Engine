#pragma once

/// ****************************************************************************
/// Clustered forward GPU data
/// ****************************************************************************

// === Math includes ===
#include <CueMath.h>

// === C++ includes ===
#include <cstdint>

namespace Cue::GpuData
{
    struct ClusterGpu final
    {
        Math::float4 minPoint{};
        Math::float4 maxPoint{};
    };

    struct ClusterLightRangeGpu final
    {
        uint32_t offset = 0;
        uint32_t count = 0;
        uint32_t padding0 = 0;
        uint32_t padding1 = 0;
    };
} // namespace Cue::GpuData
