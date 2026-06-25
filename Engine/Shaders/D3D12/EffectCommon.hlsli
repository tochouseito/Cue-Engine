// Shared declarations for CPU-authored effect primitive passes.

struct EffectSprite
{
    float3 position;
    float rotation;
    float2 size;
    float2 uvMin;
    float2 uvMax;
    uint textureId;
    uint useTexture;
    uint blendMode;
    uint padding0;
    float4 color;
};

struct EffectRibbon
{
    float3 startPosition;
    float startWidth;
    float3 endPosition;
    float endWidth;
    float4 startColor;
    float4 endColor;
    float2 uvScaleOffset;
    uint segmentCount;
    uint textureId;
    uint useTexture;
    uint blendMode;
    uint randomSeed;
    float jitter;
};

float hash11(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return (float)(value & 0x00ffffffu) / 16777215.0f;
}

float2 rotate2(float2 value, float angle)
{
    const float s = sin(angle);
    const float c = cos(angle);
    return float2(
        value.x * c - value.y * s,
        value.x * s + value.y * c);
}
