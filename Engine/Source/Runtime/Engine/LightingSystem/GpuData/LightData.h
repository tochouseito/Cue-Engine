#pragma once

/// ****************************************************************************
/// Point light GPU data
/// ****************************************************************************

// === Math includes ===
#include <CueMath.h>

// === C++ includes ===
#include <cstdint>

namespace Cue::GpuData
{
    struct LightFrameGpu final
    {
        Math::float4 ambientColorIntensity =
            Math::float4(1.0f, 1.0f, 1.0f, 0.18f);
        uint32_t directionalLightCount = 0;
        uint32_t pointLightCount = 0;
        uint32_t spotLightCount = 0;
        uint32_t padding = 0;
    };

    struct PointLightGpu final
    {
        Math::float4 positionRange =
            Math::float4(0.0f, 0.0f, 0.0f, 10.0f);
        Math::float4 colorIntensity =
            Math::float4(1.0f, 1.0f, 1.0f, 1.0f);
    };
}
