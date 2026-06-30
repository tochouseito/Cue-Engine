// StaticMesh indirect path の最小 forward shader。
// MeshPool の position stream と CPU batching が作った object index list だけで描画する。

struct VsInput
{
    float4 position : POSITION;
};

struct VsOutput
{
    float4 position : SV_POSITION;
    float3 color : COLOR0;
};

struct ObjectTransformGpu
{
    row_major float4x4 worldMatrix;
    row_major float4x4 normalMatrix;
};

cbuffer DrawObjectIndexConstants : register(b0)
{
    uint g_drawObjectStartIndex;
};

cbuffer ViewProjectionBuffer : register(b1)
{
    row_major float4x4 g_view;
    row_major float4x4 g_projection;
    float4 g_cameraPosition;
};

StructuredBuffer<uint> g_staticMeshObjectIndices : register(t0);
StructuredBuffer<ObjectTransformGpu> g_transforms : register(t1);

VsOutput vs_main(VsInput input, uint instanceId : SV_InstanceID)
{
    const uint objectIndex =
        g_staticMeshObjectIndices[g_drawObjectStartIndex + instanceId];
    const float colorPhase = (float)((objectIndex % 7u) + 1u) / 7.0f;
    const float4 worldPosition = mul(input.position, g_transforms[objectIndex].worldMatrix);
    const float4 viewPosition = mul(worldPosition, g_view);

    VsOutput output;
    output.position = mul(viewPosition, g_projection);
    output.color = saturate(abs(input.position.xyz) + float3(0.15f, 0.1f + colorPhase * 0.2f, 0.25f));
    return output;
}

float4 ps_main(VsOutput input) : SV_Target0
{
    return float4(input.color, 1.0f);
}
