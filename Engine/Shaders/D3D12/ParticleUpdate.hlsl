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

    particle.previousPosition = particle.position;

    if (particle.drag > 0.0f)
    {
        particle.velocity *= max(1.0f - particle.drag * g_frame.deltaTime, 0.0f);
    }

    if (particle.attractorStrength != 0.0f)
    {
        const float3 toAttractor = particle.attractorPosition - particle.position;
        const float distanceSq = max(dot(toAttractor, toAttractor), 0.0001f);
        particle.velocity +=
            normalize(toAttractor) *
            particle.attractorStrength *
            rsqrt(distanceSq) *
            g_frame.deltaTime;
    }

    if (particle.vortexStrength != 0.0f)
    {
        const float3 radial = particle.position - particle.attractorPosition;
        const float3 tangent = normalize(cross(float3(0.0f, 1.0f, 0.0f), radial));
        particle.velocity += tangent * particle.vortexStrength * g_frame.deltaTime;
    }

    if (particle.noiseStrength != 0.0f)
    {
        const uint noiseSeed =
            (uint)(particle.position.x * 131.0f * particle.noiseFrequency +
                   particle.position.y * 197.0f * particle.noiseFrequency +
                   particle.position.z * 251.0f * particle.noiseFrequency +
                   g_frame.time * 307.0f);
        particle.velocity +=
            random_unit3(noiseSeed) * particle.noiseStrength * g_frame.deltaTime;
    }

    particle.velocity += particle.acceleration * g_frame.deltaTime;
    particle.velocity += particle.force * g_frame.deltaTime;
    particle.position += particle.velocity * g_frame.deltaTime;
    g_particles[particleIndex] = particle;
}
