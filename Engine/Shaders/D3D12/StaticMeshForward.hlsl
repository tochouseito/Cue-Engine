#include "DrawCommon.hlsli"

struct VsOut
{
    float4 position : SV_POSITION;
    float3 worldNormal : NORMAL0;
    float2 texcoord : TEXCOORD0;
    nointerpolation uint materialId : TEXCOORD1;
};

static const float k_pi = 3.14159265359f;

cbuffer ViewProjection : register(b0)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
}

struct DrawObjectIndexConstants
{
    uint drawObjectIndex;
};

ConstantBuffer<DrawObjectIndexConstants> g_drawObjectIndex : register(b1);

StructuredBuffer<RenderObject> g_renderObjects : register(t0);
StructuredBuffer<Transform> g_transforms : register(t1);
StructuredBuffer<float4> g_positions : register(t2);
StructuredBuffer<float2> g_uvs : register(t3);
StructuredBuffer<float3> g_normals : register(t4);
StructuredBuffer<uint> g_indices : register(t5);
StructuredBuffer<MeshRange> g_meshRanges : register(t6);
ByteAddressBuffer g_renderObjectCount : register(t7);
StructuredBuffer<Material> g_materials : register(t8);

VsOut vs_main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const uint renderObjectCount = g_renderObjectCount.Load(0);
    const bool useIndexedDrawPath =
        g_drawObjectIndex.drawObjectIndex != 0xffffffffu;
    const uint renderObjectIndex =
        useIndexedDrawPath ? g_drawObjectIndex.drawObjectIndex : instanceId;
    if (renderObjectIndex >= renderObjectCount)
    {
        VsOut emptyOutput;
        emptyOutput.position = float4(-2.0f, -2.0f, -2.0f, 1.0f);
        emptyOutput.worldNormal = float3(0.0f, 0.0f, 1.0f);
        emptyOutput.texcoord = float2(0.0f, 0.0f);
        emptyOutput.materialId = 0;
        return emptyOutput;
    }

    const RenderObject renderObject = g_renderObjects[renderObjectIndex];
    const Transform transform = g_transforms[renderObject.transformId];
    const uint meshVertexIndex = vertexId;
    const float4 localPosition = g_positions[meshVertexIndex];
    const float2 localUv = g_uvs[meshVertexIndex];
    const float3 localNormal = g_normals[meshVertexIndex];

    const float4 worldPosition = mul(localPosition, transform.worldMatrix);
    const float3 worldNormal = normalize(mul(float4(localNormal, 0.0f), transform.worldMatrix).xyz);

    VsOut output;
    output.position = mul(mul(worldPosition, g_viewMatrix), g_projectionMatrix);
    output.worldNormal = worldNormal;
    output.texcoord = localUv;
    output.materialId = renderObject.materialId;
    return output;
}

float4 ps_main(VsOut input) : SV_Target0
{
    const float3 lightDirection = normalize(float3(-0.4f, -0.7f, -0.6f));
    const float ndotl = saturate(dot(normalize(input.worldNormal), -lightDirection));
    const float3 baseColor = g_materials[input.materialId].color.rgb;
    const float3 color = baseColor * (0.2f + ndotl * 0.8f);
    return float4(color, 1.0f);
}
