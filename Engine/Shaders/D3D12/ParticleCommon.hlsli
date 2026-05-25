struct ParticleFrame
{
    float deltaTime;
    float time;
    uint emitterCount;
    uint particleCount;
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
    float4 endColor;
    float4 sizeLifetime;
    uint textureId;
    uint useTexture;
    uint randomSeed;
    uint billboardMode;
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
    float4 endColor;
    float endSize;
    uint textureId;
    uint useTexture;
    uint isAlive;
};

uint hash_u32(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

float random01(uint seed)
{
    return (float)(hash_u32(seed) & 0x00ffffffu) / 16777215.0f;
}

float random_range(uint seed, float minValue, float maxValue)
{
    return lerp(minValue, maxValue, random01(seed));
}

float3 random_range3(uint seed, float3 minValue, float3 maxValue)
{
    return float3(
        random_range(seed + 11u, minValue.x, maxValue.x),
        random_range(seed + 23u, minValue.y, maxValue.y),
        random_range(seed + 37u, minValue.z, maxValue.z));
}
