// Draw the editor grid procedurally so no mesh asset is required.

static const uint kHalfGridLineCount = 50;
static const uint kGridLineCount = kHalfGridLineCount * 2 + 1;
static const float kGridExtent = 50.0f;

cbuffer ViewProjection : register(b0)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
}

struct VsOut
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
};

// Vertex entry point keeps per-pass object expansion on the GPU.
VsOut vs_main(uint vertexId : SV_VertexID)
{
    const uint lineIndex = vertexId / 2;
    const uint endpoint = vertexId & 1;
    const bool isXLine = lineIndex < kGridLineCount;
    const uint localLineIndex = isXLine ? lineIndex : lineIndex - kGridLineCount;
    const float lineCoord =
        (float)localLineIndex - (float)kHalfGridLineCount;
    const float edgeCoord = endpoint == 0 ? -kGridExtent : kGridExtent;

    float3 worldPosition;
    if (isXLine)
    {
        worldPosition = float3(edgeCoord, 0.0f, lineCoord);
    }
    else
    {
        worldPosition = float3(lineCoord, 0.0f, edgeCoord);
    }

    float4 color = float4(0.33f, 0.35f, 0.38f, 0.45f);
    if (abs(worldPosition.z) < 0.001f && isXLine)
    {
        color = float4(0.72f, 0.18f, 0.18f, 0.9f);
    }
    if (abs(worldPosition.x) < 0.001f && !isXLine)
    {
        color = float4(0.18f, 0.46f, 0.82f, 0.9f);
    }

    VsOut output;
    const float4 viewPosition = mul(float4(worldPosition, 1.0f), g_viewMatrix);
    output.position = mul(viewPosition, g_projectionMatrix);
    output.color = color;
    return output;
}

// Pixel entry point resolves the pass output without changing upstream buffers.
float4 ps_main(VsOut input) : SV_Target0
{
    return input.color;
}
