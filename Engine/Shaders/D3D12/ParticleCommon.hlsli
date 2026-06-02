// Share particle layouts and helpers across compute and draw passes to keep CPU/GPU contracts identical.

struct ParticleFrame
{
    float deltaTime;
    float time;
    uint emitterCount;
    uint particleCount;
    uint trailFrameIndex;
    uint maxTrailSegmentCount;
    uint frameReserved0;
    uint frameReserved1;
};

struct ParticleEmitter
{
    float3 position;
    uint particleBase;
    float3 velocityMin;
    uint particleCapacity;
    float3 velocityMax;
    uint spawnCount;
    float3 acceleration;
    uint spawnCursor;
    float4 startColor;
    float4 midColor;
    float4 endColor;
    float4 sizeLifetime;
    float4 curveParams;
    float4 shapeParams;
    float4 trailParams;
    float4 forceParams;
    float4 attractorParams;
    float4 linearForce;
    uint textureId;
    uint useTexture;
    uint rendererType;
    uint shapeType;
    uint randomSeed;
    uint billboardMode;
    uint reserved0;
    uint reserved1;
};

struct Particle
{
    float3 position;
    float age;
    float3 velocity;
    float lifetime;
    float3 acceleration;
    float startSize;
    float4 startColor;
    float4 midColor;
    float4 endColor;
    float endSize;
    float midSize;
    float curveMidTime;
    float trailWidth;
    float trailLength;
    uint textureId;
    uint useTexture;
    uint isAlive;
    uint rendererType;
    uint trailSegmentCount;
    uint meshId;
    float drag;
    float noiseStrength;
    float meshScale;
    float3 force;
    float noiseFrequency;
    float3 attractorPosition;
    float attractorStrength;
    float vortexStrength;
    float3 previousPosition;
};

struct ParticleTrailPoint
{
    float3 position;
    float lifeRate;
};

// Hashing keeps particle randomization deterministic across GPU vendors.
uint hash_u32(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

// Random helpers derive repeatable values from explicit seeds instead of hidden state.
float random01(uint seed)
{
    return (float)(hash_u32(seed) & 0x00ffffffu) / 16777215.0f;
}

// Range mapping keeps emitter parameters in authoring units while reusing normalized randomness.
float random_range(uint seed, float minValue, float maxValue)
{
    return lerp(minValue, maxValue, random01(seed));
}

// Vector range mapping keeps per-axis emitter variation compact in shader code.
float3 random_range3(uint seed, float3 minValue, float3 maxValue)
{
    return float3(
        random_range(seed + 11u, minValue.x, maxValue.x),
        random_range(seed + 23u, minValue.y, maxValue.y),
        random_range(seed + 37u, minValue.z, maxValue.z));
}

// Unit-vector generation gives forces and spawn directions deterministic spread without lookup textures.
float3 random_unit3(uint seed)
{
    const float3 value = random_range3(
        seed,
        float3(-1.0f, -1.0f, -1.0f),
        float3(1.0f, 1.0f, 1.0f));
    const float lengthSq = max(dot(value, value), 0.0001f);
    return value * rsqrt(lengthSq);
}

// Curve evaluation keeps particle lifetime shaping on the GPU.
float evaluate_curve(
    float startValue,
    float midValue,
    float endValue,
    float time,
    float midTime)
{
    const float clampedMidTime = clamp(midTime, 0.001f, 0.999f);
    if (time <= clampedMidTime)
    {
        return lerp(startValue, midValue, saturate(time / clampedMidTime));
    }

    return lerp(
        midValue,
        endValue,
        saturate((time - clampedMidTime) / (1.0f - clampedMidTime)));
}

// Four-channel curve evaluation lets color and packed parameters share one interpolation path.
float4 evaluate_curve4(
    float4 startValue,
    float4 midValue,
    float4 endValue,
    float time,
    float midTime)
{
    const float clampedMidTime = clamp(midTime, 0.001f, 0.999f);
    if (time <= clampedMidTime)
    {
        return lerp(startValue, midValue, saturate(time / clampedMidTime));
    }

    return lerp(
        midValue,
        endValue,
        saturate((time - clampedMidTime) / (1.0f - clampedMidTime)));
}
