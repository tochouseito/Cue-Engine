// Spawn particles from emitter ranges on the GPU so bursts do not stall the main thread.

#include "ParticleCommon.hlsli"

ConstantBuffer<ParticleFrame> g_frame : register(b0);
StructuredBuffer<ParticleEmitter> g_emitters : register(t0);
RWStructuredBuffer<Particle> g_particles : register(u0);

[numthreads(64, 1, 1)]
// Compute entry point runs one logical item per dispatch thread to avoid CPU-side iteration.
void cs_main(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
    const uint emitterIndex = groupId.x;
    if (emitterIndex >= g_frame.emitterCount)
    {
        return;
    }

    const ParticleEmitter emitter = g_emitters[emitterIndex];
    const uint localIndex = groupThreadId.x;
    if (localIndex >= emitter.spawnCount || emitter.particleCapacity == 0)
    {
        return;
    }

    const uint cursor = (emitter.spawnCursor + localIndex) % emitter.particleCapacity;
    const uint particleIndex = emitter.particleBase + cursor;
    if (particleIndex >= g_frame.particleCount)
    {
        return;
    }

    const uint seed =
        emitter.randomSeed ^
        hash_u32(particleIndex + emitter.spawnCursor * 977u + localIndex * 131u);

    Particle particle = (Particle)0;
    particle.position = emitter.position;
    if (emitter.shapeType == 1u)
    {
        const float radius =
            emitter.shapeParams.x * pow(random01(seed + 149u), 0.3333333f);
        particle.position += random_unit3(seed + 151u) * radius;
    }
    else if (emitter.shapeType == 2u)
    {
        particle.position += random_range3(
            seed + 163u,
            -emitter.shapeParams.xyz,
            emitter.shapeParams.xyz);
    }
    else if (emitter.shapeType == 3u)
    {
        const float angle = random_range(seed + 173u, 0.0f, 6.2831853f);
        const float radius = emitter.shapeParams.x * sqrt(random01(seed + 181u));
        particle.position.xz += float2(cos(angle), sin(angle)) * radius;
    }
    particle.age = 0.0f;
    particle.velocity =
        random_range3(seed + 3u, emitter.velocityMin, emitter.velocityMax);
    if (emitter.shapeType == 3u)
    {
        const float spread = tan(radians(emitter.shapeParams.y));
        const float3 coneDirection = normalize(float3(
            random_range(seed + 191u, -spread, spread),
            1.0f,
            random_range(seed + 193u, -spread, spread)));
        particle.velocity = coneDirection * length(particle.velocity);
    }
    particle.lifetime = max(
        random_range(seed + 71u, emitter.sizeLifetime.z, emitter.sizeLifetime.w),
        0.001f);
    particle.acceleration = emitter.acceleration;
    particle.startSize = emitter.sizeLifetime.x;
    particle.startColor = emitter.startColor;
    particle.midColor = emitter.midColor;
    particle.endColor = emitter.endColor;
    particle.endSize = emitter.sizeLifetime.y;
    particle.midSize = emitter.curveParams.x;
    particle.curveMidTime = emitter.curveParams.y;
    particle.trailWidth = emitter.trailParams.x;
    particle.trailLength = emitter.trailParams.y;
    particle.trailSegmentCount = max((uint)emitter.trailParams.z, 1u);
    particle.meshScale = max(emitter.trailParams.w, 0.001f);
    particle.meshId = 0u;
    particle.drag = emitter.forceParams.x;
    particle.noiseStrength = emitter.forceParams.y;
    particle.noiseFrequency = max(emitter.forceParams.z, 0.001f);
    particle.vortexStrength = emitter.forceParams.w;
    particle.force = emitter.linearForce.xyz;
    particle.attractorPosition = emitter.attractorParams.xyz;
    particle.attractorStrength = emitter.attractorParams.w;
    particle.previousPosition = particle.position;
    particle.textureId = emitter.textureId;
    particle.useTexture = emitter.useTexture;
    particle.isAlive = 1u;
    particle.rendererType = emitter.rendererType;
    g_particles[particleIndex] = particle;
}
