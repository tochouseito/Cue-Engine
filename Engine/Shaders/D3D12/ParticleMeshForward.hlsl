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

static const float3 k_vertices[12] =
{
    float3(-0.5f, -0.5f, 0.0f),
    float3(0.5f, -0.5f, 0.0f),
    float3(-0.5f, 0.5f, 0.0f),
    float3(-0.5f, 0.5f, 0.0f),
    float3(0.5f, -0.5f, 0.0f),
    float3(0.5f, 0.5f, 0.0f),
    float3(0.0f, -0.5f, -0.5f),
    float3(0.0f, -0.5f, 0.5f),
    float3(0.0f, 0.5f, -0.5f),
    float3(0.0f, 0.5f, -0.5f),
    float3(0.0f, -0.5f, 0.5f),
    float3(0.0f, 0.5f, 0.5f),
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
    if (particle.isAlive == 0u || particle.rendererType != 3u)
    {
        VsOut emptyOutput = (VsOut)0;
        emptyOutput.position = float4(0.0f, 0.0f, 0.0f, 1.0f);
        emptyOutput.color = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return emptyOutput;
    }

    const float lifeRate = particle.lifetime > 0.0f
        ? saturate(particle.age / particle.lifetime)
        : 0.0f;
    const float size = evaluate_curve(
        particle.startSize,
        particle.midSize,
        particle.endSize,
        lifeRate,
        particle.curveMidTime) * particle.meshScale;

    float4 worldPosition =
        float4(particle.position + k_vertices[vertexId] * size, 1.0f);
    float4 viewPosition = mul(worldPosition, g_viewMatrix);

    VsOut output;
    output.position = mul(viewPosition, g_projectionMatrix);
    output.texcoord = k_uvs[vertexId % 6u];
    output.color = evaluate_curve4(
        particle.startColor,
        particle.midColor,
        particle.endColor,
        lifeRate,
        particle.curveMidTime);
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
