cbuffer ViewProjection : register(b0)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
}

cbuffer DebugSelection : register(b1)
{
    row_major float4x4 g_worldMatrix;
    float4 g_color;
    uint g_isEnabled;
    uint g_padding0;
    uint g_padding1;
    uint g_padding2;
}

static const float3 kCorners[8] =
{
    { -0.5f, -0.5f, -0.5f },
    { 0.5f, -0.5f, -0.5f },
    { 0.5f, -0.5f, 0.5f },
    { -0.5f, -0.5f, 0.5f },
    { -0.5f, 0.5f, -0.5f },
    { 0.5f, 0.5f, -0.5f },
    { 0.5f, 0.5f, 0.5f },
    { -0.5f, 0.5f, 0.5f },
};

static const uint kLineVertexToCorner[24] =
{
    0, 1, 1, 2, 2, 3, 3, 0,
    4, 5, 5, 6, 6, 7, 7, 4,
    0, 4, 1, 5, 2, 6, 3, 7,
};

struct VsOut
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
};

VsOut vs_main(uint vertexId : SV_VertexID)
{
    const uint cornerIndex = kLineVertexToCorner[vertexId];
    float4 localPosition = float4(kCorners[cornerIndex], 1.0f);
    if (g_isEnabled == 0)
    {
        localPosition = float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    const float4 worldPosition = mul(localPosition, g_worldMatrix);
    const float4 viewPosition = mul(worldPosition, g_viewMatrix);

    VsOut output;
    output.position = mul(viewPosition, g_projectionMatrix);
    output.color = g_isEnabled == 0 ? float4(0.0f, 0.0f, 0.0f, 0.0f) : g_color;
    return output;
}

float4 ps_main(VsOut input) : SV_Target0
{
    return input.color;
}
