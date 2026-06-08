// Full-screen copy pass。
// offscreen render target を swapchain/backbuffer へコピーするための
// 最小 shader。full-screen triangle なので vertex buffer は不要。

static const uint kNumVertex = 3;
static const float4 kPositions[kNumVertex] =
{
    { -1.0f, 1.0f, 0.0f, 1.0f }, // left top
    { 3.0f, 1.0f, 0.0f, 1.0f }, // right top
    { -1.0f, -3.0f, 0.0f, 1.0f }, // left bottom
};
static const float2 kTexcoords[kNumVertex] =
{
    { 0.0f, 0.0f }, // left top
    { 2.0f, 0.0f }, // right top
    { 0.0f, 2.0f }, // left bottom
};

struct VsOut
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

// Vertex entry point keeps per-pass object expansion on the GPU.
VsOut vs_main(uint vertexId : SV_VertexID)
{
    VsOut output;
    output.position = kPositions[vertexId];
    output.texcoord = kTexcoords[vertexId];
    return output;
}

Texture2D<float4> gTexture : register(t0);

// Pixel entry point resolves the pass output without changing upstream buffers.
float4 ps_main(VsOut input) : SV_Target0
{
    const uint2 pixel = uint2(input.position.xy);
    return gTexture.Load(int3(pixel, 0));
}
