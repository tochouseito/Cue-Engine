// Preview spot shadow atlas tiles without changing the runtime shadow data layout.

#include "DrawCommon.hlsli"

struct VsOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

Texture2D<float> g_spotShadowMap : register(t0);
StructuredBuffer<SpotShadowFrame> g_spotShadowFrames : register(t1);

// Vertex entry point keeps per-pass object expansion on the GPU.
VsOut vs_main(uint vertexId : SV_VertexID)
{
    const float2 positions[3] =
    {
        float2(-1.0f, -1.0f),
        float2(-1.0f, 3.0f),
        float2(3.0f, -1.0f)
    };

    const float2 uvs[3] =
    {
        float2(0.0f, 1.0f),
        float2(0.0f, -1.0f),
        float2(2.0f, 1.0f)
    };

    VsOut output;
    output.position = float4(positions[vertexId], 0.0f, 1.0f);
    output.uv = uvs[vertexId];
    return output;
}

// Linearized preview depth makes atlas inspection readable in editor UI.
float linearize_shadow_depth(float depth, SpotShadowFrame shadowFrame)
{
    const float ndcDepth = depth * 2.0f - 1.0f;
    const float projectionA = shadowFrame.projection._33;
    const float projectionB = shadowFrame.projection._43;
    const float viewDepth =
        abs(projectionB) / max(abs(ndcDepth - projectionA), 0.000001f);
    const float nearClip = max(shadowFrame.params.z, 0.000001f);
    const float farClip =
        abs(projectionB) / max(abs(1.0f - projectionA), 0.000001f);
    return saturate((viewDepth - nearClip) / max(farClip - nearClip, 0.000001f));
}

// Pixel entry point resolves the pass output without changing upstream buffers.
float4 ps_main(VsOut input) : SV_Target0
{
    const uint atlasColumnCount = 2;
    const uint atlasRowCount = 2;
    const uint tileX = min((uint)(input.uv.x * atlasColumnCount),
        atlasColumnCount - 1);
    const uint tileY = min((uint)(input.uv.y * atlasRowCount),
        atlasRowCount - 1);
    const uint shadowIndex = tileY * atlasColumnCount + tileX;
    const SpotShadowFrame shadowFrame = g_spotShadowFrames[shadowIndex];
    if (shadowFrame.params.x < 0.5f)
    {
        return float4(0.02f, 0.02f, 0.02f, 1.0f);
    }

    uint width = 1;
    uint height = 1;
    g_spotShadowMap.GetDimensions(width, height);
    const int2 pixel = int2(
        clamp((int)(input.uv.x * width), 0, (int)width - 1),
        clamp((int)(input.uv.y * height), 0, (int)height - 1));
    const float depth = g_spotShadowMap.Load(int3(pixel, 0));
    if (depth >= 0.99999f)
    {
        return float4(0.04f, 0.04f, 0.04f, 1.0f);
    }

    const float linearDepth = linearize_shadow_depth(depth, shadowFrame);
    const float value = pow(1.0f - linearDepth, 0.45f);
    return float4(value, value, value, 1.0f);
}
