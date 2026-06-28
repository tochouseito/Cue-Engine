// Main static mesh forward pass。
// GPU culling/batching が作った indirect command と instance list を読み、
// material、directional light、clustered point light を評価して最終色を書く。

#include "ClusteredForwardLighting.hlsli"

struct RenderObject
{
    uint objectId;
    uint meshId;
    uint transformId;
    uint materialId;
    uint castsShadow;
    uint receivesShadow;
    uint shadowCasterMode;
    uint skinPaletteOffset;
    uint skinPaletteCount;
    uint drawFlags;
    uint depthBin;
    uint padding;
    float4 boundsCenterRadius;
};

struct Transform
{
    row_major float4x4 worldMatrix;
    row_major float4x4 normalMatrix;
};

struct VsInput
{
    float4 position : POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

struct VsOutput
{
    float4 position : SV_POSITION;
    float3 worldPosition : POSITION0;

    // pixel shader で cluster の depth slice を求めるための view-space 位置。
    // PS で worldPosition に view matrix を掛け直すより安い。
    float3 viewPosition : POSITION1;
    float3 worldNormal : NORMAL0;
    float2 texcoord : TEXCOORD0;
    nointerpolation uint materialId : TEXCOORD1;
};

cbuffer ViewProjection : register(b0)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
};

struct DrawObjectIndexConstants
{
    uint drawObjectIndex;
};

ConstantBuffer<DrawObjectIndexConstants> g_drawObjectIndex : register(b1);

StructuredBuffer<RenderObject> g_renderObjects : register(t0);
StructuredBuffer<Transform> g_transforms : register(t1);
ByteAddressBuffer g_renderObjectCount : register(t2);
StructuredBuffer<Material> g_materials : register(t3);
ConstantBuffer<LightFrame> g_lightFrame : register(b2);
StructuredBuffer<DirectionalLight> g_directionalLights : register(t4);
StructuredBuffer<PointLight> g_pointLights : register(t5);
StructuredBuffer<uint> g_renderObjectIndices : register(t6);
StructuredBuffer<ClusterLightRange> g_clusterLightRanges : register(t7);
StructuredBuffer<uint> g_clusterLightIndices : register(t8);

cbuffer ScreenWidthParam : register(b3)
{
    uint g_screenWidth;
};

cbuffer ScreenHeightParam : register(b4)
{
    uint g_screenHeight;
};

cbuffer ClusterCountXParam : register(b5)
{
    uint g_clusterCountX;
};

cbuffer ClusterCountYParam : register(b6)
{
    uint g_clusterCountY;
};

cbuffer ClusterDepthSliceCountParam : register(b7)
{
    uint g_clusterDepthSliceCount;
};

cbuffer ClusterNearZParam : register(b8)
{
    float g_clusterNearZ;
};

cbuffer ClusterFarZParam : register(b9)
{
    float g_clusterFarZ;
};

cbuffer ClusterInvLogFarNearParam : register(b10)
{
    float g_clusterInvLogFarNear;
};

#define CUE_CLUSTERED_FORWARD_LIGHTING_IMPLEMENTATION
#include "ClusteredForwardLighting.hlsli"

VsOutput build_vs_output(VsInput input, uint renderObjectIndex);

VsOutput vs_main(VsInput input, uint instanceId : SV_InstanceID)
{
    const uint renderObjectIndex =
        g_renderObjectIndices[g_drawObjectIndex.drawObjectIndex + instanceId];
    return build_vs_output(input, renderObjectIndex);
}

VsOutput range_vs_main(VsInput input)
{
    return build_vs_output(input, g_drawObjectIndex.drawObjectIndex);
}

VsOutput build_vs_output(VsInput input, uint renderObjectIndex)
{
    // 描画対象情報を取得する
    const RenderObject renderObject = g_renderObjects[renderObjectIndex];
    const Transform transform = g_transforms[renderObject.transformId];

    VsOutput output;
    output.texcoord = input.texcoord;
    output.materialId = renderObject.materialId;

    if ((renderObject.drawFlags & 1u) != 0u)
    {
        // LOD4 impostor は mesh 頂点ではなく camera-facing billboard として描く。
        // view-space 上で quad を広げることで、常に camera に正対させる。
        const float4 worldCenter =
            float4(renderObject.boundsCenterRadius.xyz, 1.0f);
        const float objectScale = length(transform.worldMatrix[0].xyz);
        float4 viewPosition = mul(worldCenter, g_viewMatrix);
        viewPosition.xy += input.position.xy * objectScale;

        output.position = mul(viewPosition, g_projectionMatrix);
        output.worldPosition = worldCenter.xyz;
        output.viewPosition = viewPosition.xyz;
        output.worldNormal = float3(0.0f, 0.0f, 1.0f);
        return output;
    }

    // 通常 mesh の頂点変換。viewPosition もここで作り、PS の per-pixel
    // matrix multiply を避ける。
    const float4 worldPosition = mul(input.position, transform.worldMatrix);
    const float4 viewPosition = mul(worldPosition, g_viewMatrix);
    const float3 worldNormal =
        normalize(mul(float4(input.normal, 0.0f), transform.normalMatrix).xyz);

    output.position = mul(viewPosition, g_projectionMatrix);
    output.worldPosition = worldPosition.xyz;
    output.viewPosition = viewPosition.xyz;
    output.worldNormal = worldNormal;
    return output;
}

float4 ps_main(VsOutput input) : SV_Target0
{
    const Material material = g_materials[input.materialId];
    const float3 normal = normalize(input.worldNormal);
    const float3 lighting =
        max(cue_evaluate_clustered_lighting(input.position.xy,
                                            input.worldPosition,
                                            input.viewPosition, normal),
            0.0f);
    return float4(material.color.rgb * lighting, material.color.a);
}

float4 transparent_probe_ps_main(VsOutput input) : SV_Target0
{
    float4 color = ps_main(input);
    color.a = 0.35f;
    return color;
}
