// Render CPU-authored effect ribbons as camera-facing segmented strips.

#include "EffectCommon.hlsli"

cbuffer ViewProjection : register(b0)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
}

StructuredBuffer<EffectRibbon> g_ribbons : register(t0);
Texture2D<float4> g_textures[] : register(t0, space1);

struct EffectPassConstants
{
    uint blendMode;
};

ConstantBuffer<EffectPassConstants> g_pass : register(b1);

struct VsOut
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
    nointerpolation uint textureId : TEXCOORD1;
    nointerpolation uint useTexture : TEXCOORD2;
};

static const float2 k_stripCorners[6] =
{
    float2(0.0f, -1.0f),
    float2(1.0f, -1.0f),
    float2(0.0f, 1.0f),
    float2(0.0f, 1.0f),
    float2(1.0f, -1.0f),
    float2(1.0f, 1.0f),
};

VsOut make_empty_output()
{
    VsOut output = (VsOut)0;
    output.position = float4(0.0f, 0.0f, 0.0f, 1.0f);
    output.color = float4(0.0f, 0.0f, 0.0f, 0.0f);
    return output;
}

float3 ribbon_point(EffectRibbon ribbon, float t)
{
    const float3 basePosition = lerp(
        ribbon.startPosition,
        ribbon.endPosition,
        t);
    const float randomX = hash11(ribbon.randomSeed + (uint)(t * 97.0f) + 13u) *
        2.0f - 1.0f;
    const float randomZ = hash11(ribbon.randomSeed + (uint)(t * 89.0f) + 41u) *
        2.0f - 1.0f;
    const float envelope = sin(saturate(t) * 3.14159265f);
    return basePosition +
        float3(randomX, 0.0f, randomZ) * ribbon.jitter * envelope;
}

VsOut vs_main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const EffectRibbon ribbon = g_ribbons[instanceId];
    if (ribbon.blendMode != g_pass.blendMode)
    {
        return make_empty_output();
    }

    const uint segmentCount = clamp(ribbon.segmentCount, 1u, 16u);
    const uint segmentIndex = vertexId / 6u;
    if (segmentIndex >= segmentCount)
    {
        return make_empty_output();
    }

    const uint localVertexId = vertexId % 6u;
    const float2 corner = k_stripCorners[localVertexId];
    const float t0 = (float)segmentIndex / (float)segmentCount;
    const float t1 = (float)(segmentIndex + 1u) / (float)segmentCount;
    const float t = lerp(t0, t1, corner.x);

    const float3 headPosition = ribbon_point(ribbon, t1);
    const float3 tailPosition = ribbon_point(ribbon, t0);
    float4 headView = mul(float4(headPosition, 1.0f), g_viewMatrix);
    float4 tailView = mul(float4(tailPosition, 1.0f), g_viewMatrix);

    const float2 segment = headView.xy - tailView.xy;
    const float2 side = dot(segment, segment) > 0.000001f
        ? normalize(float2(-segment.y, segment.x))
        : float2(1.0f, 0.0f);

    float4 viewPosition = lerp(tailView, headView, corner.x);
    const float width = lerp(ribbon.startWidth, ribbon.endWidth, t);
    viewPosition.xy += side * corner.y * width * 0.5f;

    VsOut output;
    output.position = mul(viewPosition, g_projectionMatrix);
    output.texcoord = float2(
        t * ribbon.uvScaleOffset.x + ribbon.uvScaleOffset.y,
        corner.y * 0.5f + 0.5f);
    output.color = lerp(ribbon.startColor, ribbon.endColor, t);
    output.textureId = ribbon.textureId;
    output.useTexture = ribbon.useTexture;
    return output;
}

float4 ps_main(VsOut input) : SV_Target0
{
    float4 color = input.color;
    if (input.useTexture != 0u)
    {
        const uint textureIndex = NonUniformResourceIndex(input.textureId);
        uint textureWidth = 1;
        uint textureHeight = 1;
        g_textures[textureIndex].GetDimensions(textureWidth, textureHeight);

        const float2 uv = saturate(input.texcoord);
        const uint2 texelCoord = uint2(
            min((uint)(uv.x * textureWidth), textureWidth - 1),
            min((uint)(uv.y * textureHeight), textureHeight - 1));
        color *= g_textures[textureIndex].Load(int3(texelCoord, 0));
    }

    if (color.a <= 0.0f)
    {
        discard;
    }

    return color;
}
