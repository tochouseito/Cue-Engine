#include "ParticleCommon.hlsli"

cbuffer ViewProjection : register(b0)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
}

StructuredBuffer<Particle> g_particles : register(t0);
Texture2D<float4> g_textures[] : register(t0, space1);

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

VsOut vs_main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const Particle particle = g_particles[instanceId];
    const float lifeRate = particle.lifetime > 0.0f
        ? saturate(particle.age / particle.lifetime)
        : 0.0f;
    const float size = lerp(particle.startSize, particle.endSize, lifeRate);

    float4 viewPosition = mul(float4(particle.position, 1.0f), g_viewMatrix);
    viewPosition.xy += k_corners[vertexId] * size;

    VsOut output;
    output.position = particle.isAlive == 0u
        ? float4(0.0f, 0.0f, 0.0f, 1.0f)
        : mul(viewPosition, g_projectionMatrix);
    output.texcoord = k_uvs[vertexId];
    output.color = particle.isAlive == 0u
        ? float4(0.0f, 0.0f, 0.0f, 0.0f)
        : lerp(particle.startColor, particle.endColor, lifeRate);
    output.textureId = particle.textureId;
    output.useTexture = particle.useTexture;
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
