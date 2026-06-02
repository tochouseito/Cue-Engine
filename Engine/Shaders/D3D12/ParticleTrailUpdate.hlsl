// Expand particle motion history into trail segments so rendering can stay append-buffer based.

#include "ParticleCommon.hlsli"

ConstantBuffer<ParticleFrame> g_frame : register(b0);
StructuredBuffer<Particle> g_particles : register(t0);
RWStructuredBuffer<ParticleTrailPoint> g_trails : register(u0);

[numthreads(64, 1, 1)]
// Compute entry point runs one logical item per dispatch thread to avoid CPU-side iteration.
void cs_main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint particleIndex = dispatchThreadId.x;
    if (particleIndex >= g_frame.particleCount)
    {
        return;
    }

    const Particle particle = g_particles[particleIndex];
    const uint maxSegmentCount = max(g_frame.maxTrailSegmentCount, 1u);
    const uint trailIndex =
        particleIndex * maxSegmentCount +
        (g_frame.trailFrameIndex % maxSegmentCount);

    ParticleTrailPoint trailPoint = (ParticleTrailPoint)0;
    trailPoint.position = particle.position;
    trailPoint.lifeRate = particle.lifetime > 0.0f
        ? saturate(particle.age / particle.lifetime)
        : 0.0f;

    if (particle.isAlive == 0u)
    {
        trailPoint.lifeRate = 1.0f;
    }

    g_trails[trailIndex] = trailPoint;
}
