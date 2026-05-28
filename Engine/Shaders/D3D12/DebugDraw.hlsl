// Render transient debug primitives from CPU-authored line and triangle buffers.

cbuffer ViewProjection : register(b0)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
}

struct VsIn
{
    float4 position : POSITION;
    float4 color : COLOR0;
};

struct VsOut
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
};

// Vertex entry point keeps per-pass object expansion on the GPU.
VsOut vs_main(VsIn input)
{
    VsOut output;
    const float4 viewPosition = mul(input.position, g_viewMatrix);
    output.position = mul(viewPosition, g_projectionMatrix);
    output.color = input.color;
    return output;
}

// Pixel entry point resolves the pass output without changing upstream buffers.
float4 ps_main(VsOut input) : SV_Target0
{
    return input.color;
}
