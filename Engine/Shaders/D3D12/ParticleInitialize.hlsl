#include "ParticleCommon.hlsli"

cbuffer DispatchParam : register(b0)
{
    uint g_particleCount;
};

RWStructuredBuffer<Particle> g_particles : register(u0);

[numthreads(64, 1, 1)]
void cs_main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint particleIndex = dispatchThreadId.x;
    if (particleIndex >= g_particleCount)
    {
        return;
    }

    g_particles[particleIndex] = (Particle)0;
}
