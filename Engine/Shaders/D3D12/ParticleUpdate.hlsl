#include "ParticleCommon.hlsli"

ConstantBuffer<ParticleFrame> g_frame : register(b0);
RWStructuredBuffer<Particle> g_particles : register(u0);

[numthreads(64, 1, 1)]
void cs_main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint particleIndex = dispatchThreadId.x;
    if (particleIndex >= g_frame.particleCount)
    {
        return;
    }

    Particle particle = g_particles[particleIndex];
    if (particle.isAlive == 0u)
    {
        return;
    }

    particle.age += g_frame.deltaTime;
    if (particle.age >= particle.lifetime)
    {
        particle.isAlive = 0u;
        particle.age = 0.0f;
        g_particles[particleIndex] = particle;
        return;
    }

    particle.velocity += particle.acceleration * g_frame.deltaTime;
    particle.position += particle.velocity * g_frame.deltaTime;
    g_particles[particleIndex] = particle;
}
