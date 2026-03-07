struct VsOut
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

VsOut vs_main(uint vertexId : SV_VertexID)
{
    VsOut output = (VsOut)0;

    const float2 positions[3] =
    {
        float2(0.0f, 0.70f),
        float2(0.70f, -0.70f),
        float2(-0.70f, -0.70f),
    };

    const float3 colors[3] =
    {
        float3(1.0f, 0.1f, 0.1f),
        float3(0.1f, 1.0f, 0.1f),
        float3(0.1f, 0.4f, 1.0f),
    };

    output.position = float4(positions[vertexId], 0.0f, 1.0f);
    output.color = colors[vertexId];
    return output;
}

float4 ps_main(VsOut input) : SV_Target0
{
    return float4(input.color, 1.0f);
}
