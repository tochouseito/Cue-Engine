#pragma once

// === Math includes ===
#include <CueMath.h>

// === C++ includes ===
#include <cstdint>

namespace Cue::GpuData
{
    inline constexpr uint32_t k_maxParticleEmitterCount = 256;
    inline constexpr uint32_t k_maxParticleCount = 65536;
    inline constexpr uint32_t k_maxParticleTrailSegmentCount = 16;
    inline constexpr uint32_t k_defaultEmitterParticleCapacity = 256;

    struct ParticleFrameGpu final
    {
        float deltaTime = 0.0f;
        float time = 0.0f;
        uint32_t emitterCount = 0;
        uint32_t particleCount = 0;
        uint32_t trailFrameIndex = 0;
        uint32_t maxTrailSegmentCount = k_maxParticleTrailSegmentCount;
        uint32_t reserved0 = 0;
        uint32_t reserved1 = 0;
    };

    struct ParticleEmitterGpu final
    {
        Math::float3 position = Math::float3::zero();
        uint32_t particleBase = 0;
        Math::float3 velocityMin = Math::float3::zero();
        uint32_t particleCapacity = 0;
        Math::float3 velocityMax = Math::float3::zero();
        uint32_t spawnCount = 0;
        Math::float3 acceleration = Math::float3::zero();
        uint32_t spawnCursor = 0;
        Math::float4 startColor = Math::float4(1.0f, 1.0f, 1.0f, 1.0f);
        Math::float4 midColor = Math::float4(1.0f, 0.9f, 0.6f, 0.75f);
        Math::float4 endColor = Math::float4(1.0f, 1.0f, 1.0f, 0.0f);
        Math::float4 sizeLifetime = Math::float4(0.15f, 0.0f, 1.0f, 1.0f);
        Math::float4 curveParams = Math::float4(0.25f, 0.5f, 0.0f, 0.0f);
        Math::float4 shapeParams = Math::float4(0.0f, 0.0f, 0.0f, 0.0f);
        Math::float4 trailParams = Math::float4(0.08f, 0.45f, 4.0f, 0.0f);
        Math::float4 forceParams = Math::float4(0.0f, 0.0f, 1.0f, 0.0f);
        Math::float4 attractorParams = Math::float4(0.0f, 0.0f, 0.0f, 0.0f);
        Math::float4 linearForce = Math::float4(0.0f, 0.0f, 0.0f, 0.0f);
        uint32_t textureId = 0;
        uint32_t useTexture = 0;
        uint32_t rendererType = 0;
        uint32_t shapeType = 0;
        uint32_t randomSeed = 1;
        uint32_t billboardMode = 0;
        uint32_t reserved0 = 0;
        uint32_t reserved1 = 0;
    };

    struct ParticleGpu final
    {
        Math::float3 position = Math::float3::zero();
        float age = 0.0f;
        Math::float3 velocity = Math::float3::zero();
        float lifetime = 0.0f;
        Math::float3 acceleration = Math::float3::zero();
        float startSize = 0.0f;
        Math::float4 startColor = Math::float4(1.0f, 1.0f, 1.0f, 1.0f);
        Math::float4 midColor = Math::float4(1.0f, 0.9f, 0.6f, 0.75f);
        Math::float4 endColor = Math::float4(1.0f, 1.0f, 1.0f, 0.0f);
        float endSize = 0.0f;
        float midSize = 0.0f;
        float curveMidTime = 0.5f;
        float trailWidth = 0.0f;
        float trailLength = 0.0f;
        uint32_t textureId = 0;
        uint32_t useTexture = 0;
        uint32_t isAlive = 0;
        uint32_t rendererType = 0;
        uint32_t trailSegmentCount = 1;
        uint32_t meshId = 0;
        float drag = 0.0f;
        float noiseStrength = 0.0f;
        float meshScale = 1.0f;
        Math::float3 force = Math::float3::zero();
        float noiseFrequency = 1.0f;
        Math::float3 attractorPosition = Math::float3::zero();
        float attractorStrength = 0.0f;
        float vortexStrength = 0.0f;
        Math::float3 previousPosition = Math::float3::zero();
    };

    struct ParticleTrailPointGpu final
    {
        Math::float3 position = Math::float3::zero();
        float lifeRate = 0.0f;
    };
} // namespace Cue::GpuData
