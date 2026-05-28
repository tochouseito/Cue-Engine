// ShadowData の役割と公開要素を定義する

#pragma once

// === Base includes ===
#include <CueMath.h>

// === C++ includes ===
#include <cstdint>

namespace Cue::GpuData
{
    inline constexpr uint32_t k_directionalShadowMapSize = 2048;
    inline constexpr uint32_t k_pointShadowTileSize = 1024;
    inline constexpr uint32_t k_pointShadowAtlasColumnCount = 3;
    inline constexpr uint32_t k_pointShadowAtlasRowCount = 2;
    inline constexpr uint32_t k_pointShadowFaceCount = 6;
    inline constexpr uint32_t k_pointShadowMapWidth =
        k_pointShadowTileSize * k_pointShadowAtlasColumnCount;
    inline constexpr uint32_t k_pointShadowMapHeight =
        k_pointShadowTileSize * k_pointShadowAtlasRowCount;
    inline constexpr uint32_t k_spotShadowTileSize = 1024;
    inline constexpr uint32_t k_spotShadowAtlasColumnCount = 2;
    inline constexpr uint32_t k_spotShadowAtlasRowCount = 2;
    inline constexpr uint32_t k_maxSpotShadowCount =
        k_spotShadowAtlasColumnCount * k_spotShadowAtlasRowCount;
    inline constexpr uint32_t k_spotShadowMapSize =
        k_spotShadowTileSize * k_spotShadowAtlasColumnCount;
    inline constexpr uint32_t k_invalidShadowLightIndex = 0xffffffffu;

    struct SpotShadowFrameGpu final
    {
        Math::float4x4 view;
        Math::float4x4 projection;
        Math::float4 atlas = Math::float4(0.0f, 0.0f, 0.5f, 0.5f);
        Math::float4 params = Math::float4(
            0.0f,
            0.002f,
            0.05f,
            static_cast<float>(k_invalidShadowLightIndex));
        Math::float4 tuning = Math::float4(
            static_cast<float>(k_spotShadowTileSize),
            0.75f,
            1.0f,
            0.004f);
    };

    struct DirectionalShadowFrameGpu final
    {
        Math::float4x4 view;
        Math::float4x4 projection;
        Math::float4 params = Math::float4(
            0.0f,
            0.001f,
            0.0f,
            static_cast<float>(k_invalidShadowLightIndex));
        Math::float4 tuning = Math::float4(
            static_cast<float>(k_directionalShadowMapSize),
            0.75f,
            1.0f,
            0.002f);
    };

    struct PointShadowFaceGpu final
    {
        Math::float4x4 view;
        Math::float4x4 projection;
        Math::float4 atlas = Math::float4(0.0f, 0.0f, 1.0f / 3.0f, 0.5f);
        Math::float4 params = Math::float4(
            0.0f,
            0.002f,
            0.05f,
            static_cast<float>(k_invalidShadowLightIndex));
        Math::float4 tuning = Math::float4(
            static_cast<float>(k_pointShadowTileSize),
            0.75f,
            1.0f,
            0.004f);
        Math::float4 lightPositionRange =
            Math::float4(0.0f, 0.0f, 0.0f, 10.0f);
    };
}
