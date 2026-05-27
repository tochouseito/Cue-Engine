#include "ParticleCommon.hlsli"

cbuffer DispatchParam : register(b0)
{
    uint g_particleCount;
};

cbuffer TrailDispatchParam : register(b1)
{
    uint g_trailPointCount;
};

RWStructuredBuffer<Particle> g_particles : register(u0);
RWStructuredBuffer<ParticleTrailPoint> g_trails : register(u1);

[numthreads(64, 1, 1)]
void cs_main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint particleIndex = dispatchThreadId.x;
    if (particleIndex >= g_particleCount)
    {
        if (particleIndex < g_trailPointCount)
        {
            g_trails[particleIndex] = (ParticleTrailPoint)0;
        }
        return;
    }

    g_particles[particleIndex] = (Particle)0;
    if (particleIndex < g_trailPointCount)
    {
        g_trails[particleIndex] = (ParticleTrailPoint)0;
    }
}
