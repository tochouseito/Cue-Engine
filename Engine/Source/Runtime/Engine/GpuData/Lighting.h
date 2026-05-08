#pragma once

// === Math includes ===
#include <CueMath.h>

namespace Cue::GpuData
{
    struct DirectionalLightGpu final
    {
        Math::float4 directionAndIntensity =
            Math::float4(-0.4f, -0.7f, -0.6f, 1.0f);
        Math::float4 colorAndAmbient =
            Math::float4(1.0f, 0.96f, 0.88f, 0.18f);
        Math::float4 ambientGroundAndSpecular =
            Math::float4(0.08f, 0.09f, 0.11f, 1.0f);
    };

    struct ShadowMappingGpu final
    {
        Math::float4x4 view = Math::float4x4::identity();
        Math::float4x4 projection = Math::float4x4::identity();
        Math::float4 texelSizeAndBias = Math::float4(
            1.0f / 1024.0f,
            1.0f / 1024.0f,
            0.003f,
            0.35f);
    };
}
