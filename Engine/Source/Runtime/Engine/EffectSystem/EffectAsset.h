// EffectAsset の役割と公開要素を定義する

#pragma once

// === Math includes ===
#include <CueMath.h>

// === C++ includes ===
#include <cstdint>
#include <string>
#include <vector>

namespace Cue::EffectSystem
{
    enum class EffectBillboardMode : uint32_t
    {
        View = 0,
    };

    enum class EffectRendererType : uint32_t
    {
        Billboard = 0,
        Trail = 1,
        Ribbon = 2,
        Mesh = 3,
    };

    enum class EffectEmitterShape : uint32_t
    {
        Point = 0,
        Sphere = 1,
        Box = 2,
        Cone = 3,
    };

    struct EffectEmitterDesc final
    {
        std::string name = "Emitter";
        std::string materialName{};
        std::string meshName{};
        Math::float3 positionOffset = Math::float3::zero();
        Math::float3 shapeBoxExtents = Math::float3(0.5f, 0.5f, 0.5f);
        Math::float3 linearForce = Math::float3::zero();
        Math::float3 attractorPosition = Math::float3::zero();
        Math::float3 velocityMin = Math::float3(-0.2f, 0.8f, -0.2f);
        Math::float3 velocityMax = Math::float3(0.2f, 1.6f, 0.2f);
        Math::float3 acceleration = Math::float3(0.0f, -0.4f, 0.0f);
        Math::float4 startColor = Math::float4(1.0f, 1.0f, 1.0f, 1.0f);
        Math::float4 midColor = Math::float4(1.0f, 0.9f, 0.6f, 0.75f);
        Math::float4 endColor = Math::float4(1.0f, 1.0f, 1.0f, 0.0f);
        float startSize = 0.15f;
        float midSize = 0.25f;
        float endSize = 0.0f;
        float curveMidTime = 0.5f;
        float startDelay = 0.0f;
        float duration = 2.0f;
        float shapeRadius = 0.0f;
        float shapeAngleDegrees = 30.0f;
        float trailWidth = 0.08f;
        float trailLength = 0.45f;
        float meshScale = 1.0f;
        float drag = 0.0f;
        float noiseStrength = 0.0f;
        float noiseFrequency = 1.0f;
        float attractorStrength = 0.0f;
        float vortexStrength = 0.0f;
        float minLifetime = 0.75f;
        float maxLifetime = 1.25f;
        float emitRate = 32.0f;
        uint32_t burstCount = 0;
        uint32_t trailSegmentCount = 4;
        uint32_t maxParticleCount = 256;
        uint32_t randomSeed = 1;
        EffectRendererType rendererType = EffectRendererType::Billboard;
        EffectEmitterShape shape = EffectEmitterShape::Point;
        EffectBillboardMode billboardMode = EffectBillboardMode::View;
        bool isLooping = true;
        bool isVisible = true;
    };

    struct EffectGraphNodeDesc final
    {
        std::string name = "Emitter";
        Math::float2 position = Math::float2(0.0f, 0.0f);
        uint32_t emitterIndex = 0;
    };

    struct EffectAsset final
    {
        std::string name{};
        std::vector<EffectEmitterDesc> emitters{};
        std::vector<EffectGraphNodeDesc> graphNodes{};
    };
} // namespace Cue::EffectSystem
