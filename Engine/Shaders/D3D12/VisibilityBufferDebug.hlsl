// Fullscreen visualization for the visibility buffer.

Texture2D<uint2> g_visibility : register(t0);

cbuffer DebugModeParam : register(b0)
{
    uint g_debugMode;
};

struct VsOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VsOutput vs_main(uint vertexId : SV_VertexID)
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

    VsOutput output;
    output.position = float4(positions[vertexId], 0.0f, 1.0f);
    output.uv = uvs[vertexId];
    return output;
}

float3 id_to_color(uint id)
{
    uint x = id;
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;

    const float r = ((x >> 0) & 255u) / 255.0f;
    const float g = ((x >> 8) & 255u) / 255.0f;
    const float b = ((x >> 16) & 255u) / 255.0f;
    return float3(r, g, b);
}

float4 ps_main(VsOutput input) : SV_Target0
{
    const uint2 pixel = uint2(input.position.xy);
    const uint2 id = g_visibility.Load(int3(pixel, 0));
    if (id.x == 0u)
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    if (g_debugMode == 2u)
    {
        return float4(id_to_color(id.y), 1.0f);
    }

    return float4(id_to_color(id.x - 1u), 1.0f);
}
