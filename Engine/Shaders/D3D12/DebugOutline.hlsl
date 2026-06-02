// Build a screen-space outline around selected pixels so object shaders stay unchanged.

struct VsOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

struct SelectedObjectConstants
{
    uint objectId;
};

ConstantBuffer<SelectedObjectConstants> g_selectedObject : register(b0);
Texture2D<uint> g_objectIds : register(t0);

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

// Pixel entry point resolves the pass output without changing upstream buffers.
float4 ps_main(VsOut input) : SV_Target0
{
    if (g_selectedObject.objectId == 0)
    {
        discard;
    }

    uint width = 0;
    uint height = 0;
    g_objectIds.GetDimensions(width, height);
    const int2 pixel = int2(
        clamp((int)(input.uv.x * width), 0, (int)width - 1),
        clamp((int)(input.uv.y * height), 0, (int)height - 1));

    const uint centerId = g_objectIds.Load(int3(pixel, 0));
    if (centerId == g_selectedObject.objectId)
    {
        discard;
    }

    const int2 offsets[8] =
    {
        int2(-1, 0),
        int2(1, 0),
        int2(0, -1),
        int2(0, 1),
        int2(-1, -1),
        int2(1, -1),
        int2(-1, 1),
        int2(1, 1)
    };

    bool touchesSelected = false;
    for (uint i = 0; i < 8; ++i)
    {
        const int2 neighbor = clamp(
            pixel + offsets[i],
            int2(0, 0),
            int2((int)width - 1, (int)height - 1));
        touchesSelected =
            touchesSelected ||
            g_objectIds.Load(int3(neighbor, 0)) == g_selectedObject.objectId;
    }

    if (!touchesSelected)
    {
        discard;
    }

    return float4(1.0f, 0.84f, 0.18f, 1.0f);
}
