// Draw the skybox as a depth-stable background while preserving the camera rotation only.

#include "DrawCommon.hlsli"

struct VsOut
{
    float4 position : SV_POSITION;
    float3 direction : TEXCOORD0;
};

cbuffer ViewProjection : register(b0)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
}

TextureCube<float4> g_skyboxTexture : register(t0);
SamplerState g_sampler : register(s0);

static const float3 k_cubeVertices[36] =
{
    float3(-1.0f, 1.0f, -1.0f), float3(1.0f, 1.0f, -1.0f), float3(1.0f, -1.0f, -1.0f),
    float3(-1.0f, 1.0f, -1.0f), float3(1.0f, -1.0f, -1.0f), float3(-1.0f, -1.0f, -1.0f),
    float3(1.0f, 1.0f, 1.0f), float3(-1.0f, 1.0f, 1.0f), float3(-1.0f, -1.0f, 1.0f),
    float3(1.0f, 1.0f, 1.0f), float3(-1.0f, -1.0f, 1.0f), float3(1.0f, -1.0f, 1.0f),
    float3(-1.0f, 1.0f, 1.0f), float3(-1.0f, 1.0f, -1.0f), float3(-1.0f, -1.0f, -1.0f),
    float3(-1.0f, 1.0f, 1.0f), float3(-1.0f, -1.0f, -1.0f), float3(-1.0f, -1.0f, 1.0f),
    float3(1.0f, 1.0f, -1.0f), float3(1.0f, 1.0f, 1.0f), float3(1.0f, -1.0f, 1.0f),
    float3(1.0f, 1.0f, -1.0f), float3(1.0f, -1.0f, 1.0f), float3(1.0f, -1.0f, -1.0f),
    float3(-1.0f, 1.0f, 1.0f), float3(1.0f, 1.0f, 1.0f), float3(1.0f, 1.0f, -1.0f),
    float3(-1.0f, 1.0f, 1.0f), float3(1.0f, 1.0f, -1.0f), float3(-1.0f, 1.0f, -1.0f),
    float3(-1.0f, -1.0f, -1.0f), float3(1.0f, -1.0f, -1.0f), float3(1.0f, -1.0f, 1.0f),
    float3(-1.0f, -1.0f, -1.0f), float3(1.0f, -1.0f, 1.0f), float3(-1.0f, -1.0f, 1.0f),
};

// Vertex entry point keeps per-pass object expansion on the GPU.
VsOut vs_main(uint vertexId : SV_VertexID)
{
    const float3 localPosition = k_cubeVertices[vertexId];
    float4x4 view = g_viewMatrix;
    view[3][0] = 0.0f;
    view[3][1] = 0.0f;
    view[3][2] = 0.0f;

    const float4 viewPosition = mul(float4(localPosition, 1.0f), view);
    const float4 clipPosition = mul(viewPosition, g_projectionMatrix);

    VsOut output;
    output.position = clipPosition.xyww;
    output.direction = localPosition;
    return output;
}

// Pixel entry point resolves the pass output without changing upstream buffers.
float4 ps_main(VsOut input) : SV_Target0
{
    const float3 direction = normalize(input.direction);
    return g_skyboxTexture.Sample(g_sampler, direction);
}
