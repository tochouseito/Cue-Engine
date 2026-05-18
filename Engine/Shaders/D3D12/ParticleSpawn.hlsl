#include "ParticleCommon.hlsli"

ConstantBuffer<ParticleFrame> g_frame : register(b0);
StructuredBuffer<ParticleEmitter> g_emitters : register(t0);
RWStructuredBuffer<Particle> g_particles : register(u0);

[numthreads(64, 1, 1)]
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
    particle.age = 0.0f;
    particle.velocity =
        random_range3(seed + 3u, emitter.velocityMin, emitter.velocityMax);
    particle.lifetime = max(
        random_range(seed + 71u, emitter.sizeLifetime.z, emitter.sizeLifetime.w),
        0.001f);
    particle.acceleration = emitter.acceleration;
    particle.startSize = emitter.sizeLifetime.x;
    particle.startColor = emitter.startColor;
    particle.endColor = emitter.endColor;
    particle.endSize = emitter.sizeLifetime.y;
    particle.textureId = emitter.textureId;
    particle.useTexture = emitter.useTexture;
    particle.isAlive = 1u;
    g_particles[particleIndex] = particle;
}
