// LightData の役割と公開要素を定義する

#pragma once

// === Math includes ===
#include <CueMath.h>

// === C++ includes ===
#include <cstdint>

namespace Cue::GpuData
{
    inline constexpr uint32_t k_maxDirectionalLightCount = 4;
    inline constexpr uint32_t k_maxPointLightCount = 64;
    inline constexpr uint32_t k_maxSpotLightCount = 32;

    struct LightFrameGpu final
    {
        Math::float4 ambientColorIntensity =
            Math::float4(1.0f, 1.0f, 1.0f, 0.2f);
        uint32_t directionalLightCount = 0;
        uint32_t pointLightCount = 0;
        uint32_t spotLightCount = 0;
        uint32_t padding = 0;
    };

    struct DirectionalLightGpu final
    {
        Math::float4 directionIntensity =
            Math::float4(0.0f, -1.0f, 0.0f, 1.0f);
        Math::float4 color = Math::float4(1.0f, 1.0f, 1.0f, 1.0f);
    };

    struct PointLightGpu final
    {
        Math::float4 positionRange =
            Math::float4(0.0f, 0.0f, 0.0f, 10.0f);
        Math::float4 colorIntensity =
            Math::float4(1.0f, 1.0f, 1.0f, 1.0f);
    };

    struct SpotLightGpu final
    {
        Math::float4 positionRange =
            Math::float4(0.0f, 0.0f, 0.0f, 12.0f);
        Math::float4 directionOuterCos =
            Math::float4(0.0f, -1.0f, 0.0f, 0.70710678f);
        Math::float4 colorIntensity =
            Math::float4(1.0f, 1.0f, 1.0f, 1.0f);
    };
}
