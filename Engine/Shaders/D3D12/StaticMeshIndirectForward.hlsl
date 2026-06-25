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

cbuffer DrawObjectIndexConstants : register(b0)
{
    uint g_drawObjectStartIndex;
};

StructuredBuffer<uint> g_staticMeshObjectIndices : register(t0);

VsOutput vs_main(VsInput input, uint instanceId : SV_InstanceID)
{
    const uint objectIndex =
        g_staticMeshObjectIndices[g_drawObjectStartIndex + instanceId];
    const float colorPhase = (float)((objectIndex % 7u) + 1u) / 7.0f;

    VsOutput output;
    output.position = float4(input.position.x, input.position.y, input.position.z + 0.5f, 1.0f);
    output.color = saturate(abs(input.position.xyz) + float3(0.15f, 0.1f + colorPhase * 0.2f, 0.25f));
    return output;
}

float4 ps_main(VsOutput input) : SV_Target0
{
    return float4(input.color, 1.0f);
}
