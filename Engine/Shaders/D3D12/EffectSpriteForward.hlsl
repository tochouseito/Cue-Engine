// Render CPU-authored effect sprites as view-facing quads.

#include "EffectCommon.hlsli"

cbuffer ViewProjection : register(b0)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
}

StructuredBuffer<EffectSprite> g_sprites : register(t0);
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

static const float2 k_corners[6] =
{
    float2(-0.5f, -0.5f),
    float2(0.5f, -0.5f),
    float2(-0.5f, 0.5f),
    float2(-0.5f, 0.5f),
    float2(0.5f, -0.5f),
    float2(0.5f, 0.5f),
};

static const float2 k_uvs[6] =
{
    float2(0.0f, 1.0f),
    float2(1.0f, 1.0f),
    float2(0.0f, 0.0f),
    float2(0.0f, 0.0f),
    float2(1.0f, 1.0f),
    float2(1.0f, 0.0f),
};

VsOut make_empty_output()
{
    VsOut output = (VsOut)0;
    output.position = float4(0.0f, 0.0f, 0.0f, 1.0f);
    output.color = float4(0.0f, 0.0f, 0.0f, 0.0f);
    return output;
}

VsOut vs_main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const EffectSprite sprite = g_sprites[instanceId];
    if (sprite.blendMode != g_pass.blendMode)
    {
        return make_empty_output();
    }

    float2 corner = rotate2(k_corners[vertexId] * sprite.size, sprite.rotation);
    float4 viewPosition = mul(float4(sprite.position, 1.0f), g_viewMatrix);
    viewPosition.xy += corner;

    VsOut output;
    output.position = mul(viewPosition, g_projectionMatrix);
    output.texcoord = lerp(sprite.uvMin, sprite.uvMax, k_uvs[vertexId]);
    output.color = sprite.color;
    output.textureId = sprite.textureId;
    output.useTexture = sprite.useTexture;
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
