cbuffer CameraConstants : register(b0)
{
    row_major float4x4 g_viewProjection;
};

cbuffer TransformConstants : register(b1)
{
    row_major float4x4 g_world;
};

struct VsIn
{
    float4 position : POSITION0;
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL0;
};

struct VsOut
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

VsOut vs_main(VsIn input)
{
    VsOut output = (VsOut)0;
    float4 worldPosition = mul(input.position, g_world);
    output.position = mul(worldPosition, g_viewProjection);
    output.color = (input.normal * 0.5f) + 0.5f;
    return output;
}

float4 ps_main(VsOut input) : SV_Target0
{
    return float4(input.color, 1.0f);
}
