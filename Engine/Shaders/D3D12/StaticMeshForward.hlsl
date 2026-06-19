// Main static mesh forward pass。
// GPU culling/batching が作った indirect command と instance list を読み、
// material、directional light、clustered point light を評価して最終色を書く。

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

struct Material
{
    float4 color;
    uint textureId;
    uint useTexture;
    uint useReflectionSkybox;
    float shininess;
};

struct LightFrame
{
    float4 ambientColorIntensity;
    uint directionalLightCount;
    uint pointLightCount;
    uint spotLightCount;
    uint padding;
};

struct DirectionalLight
{
    float4 directionIntensity;
    float4 color;
};

struct PointLight
{
    float4 positionRange;
    float4 colorIntensity;
};

struct ClusterLightRange
{
    // ClusterLightCulling が作った compact light list への参照。
    // pixel shader は自分の cluster の offset/count だけをたどる。
    uint offset;
    uint count;
    uint hash;
    uint overflow;
};

struct MeshRange
{
    uint indexCount;
    uint startIndex;
    int baseVertex;
    uint firstMeshlet;
    uint meshletCount;
    uint padding0;
    uint padding1;
    uint padding2;
};

struct MeshletBounds
{
    float3 center;
    float radius;
    uint firstIndex;
    uint indexCount;
    uint padding0;
    uint padding1;
};

struct VisibleMeshlet
{
    uint renderObjectIndex;
    uint meshletIndex;
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
StructuredBuffer<float4> g_positions : register(t9);
StructuredBuffer<float2> g_texcoords : register(t10);
StructuredBuffer<float3> g_normals : register(t11);
StructuredBuffer<uint> g_indices : register(t12);
StructuredBuffer<MeshRange> g_meshRanges : register(t13);
StructuredBuffer<MeshletBounds> g_meshletBounds : register(t14);
StructuredBuffer<VisibleMeshlet> g_visibleMeshlets : register(t15);

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

VsOutput clipped_output(uint materialId)
{
    VsOutput output;
    output.position = float4(0.0f, 0.0f, 0.0f, 0.0f);
    output.worldPosition = float3(0.0f, 0.0f, 0.0f);
    output.viewPosition = float3(0.0f, 0.0f, 0.0f);
    output.worldNormal = float3(0.0f, 0.0f, 1.0f);
    output.texcoord = float2(0.0f, 0.0f);
    output.materialId = materialId;
    return output;
}

VsOutput vs_main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const VisibleMeshlet visibleMeshlet =
        g_visibleMeshlets[g_drawObjectIndex.drawObjectIndex + instanceId];
    const uint renderObjectIndex = visibleMeshlet.renderObjectIndex;

    // 描画対象情報を取得する
    const RenderObject renderObject = g_renderObjects[renderObjectIndex];
    const Transform transform = g_transforms[renderObject.transformId];
    const MeshRange meshRange = g_meshRanges[renderObject.meshId];
    const MeshletBounds meshlet = g_meshletBounds[visibleMeshlet.meshletIndex];
    if (vertexId >= meshlet.indexCount)
    {
        return clipped_output(renderObject.materialId);
    }

    const uint meshletIndexAddress =
        meshRange.startIndex + meshlet.firstIndex + vertexId;
    const uint vertexIndex =
        (uint)((int)g_indices[meshletIndexAddress] + meshRange.baseVertex);
    const float4 localPosition = g_positions[vertexIndex];
    const float2 localTexcoord = g_texcoords[vertexIndex];
    const float3 localNormal = g_normals[vertexIndex];

    VsOutput output;
    output.texcoord = localTexcoord;
    output.materialId = renderObject.materialId;

    if ((renderObject.drawFlags & 1u) != 0u)
    {
        // LOD4 impostor は mesh 頂点ではなく camera-facing billboard として描く。
        // view-space 上で quad を広げることで、常に camera に正対させる。
        const float4 worldCenter =
            float4(renderObject.boundsCenterRadius.xyz, 1.0f);
        const float objectScale = length(transform.worldMatrix[0].xyz);
        float4 viewPosition = mul(worldCenter, g_viewMatrix);
        viewPosition.xy += localPosition.xy * objectScale;

        output.position = mul(viewPosition, g_projectionMatrix);
        output.worldPosition = worldCenter.xyz;
        output.viewPosition = viewPosition.xyz;
        output.worldNormal = float3(0.0f, 0.0f, 1.0f);
        return output;
    }

    // 通常 mesh の頂点変換。viewPosition もここで作り、PS の per-pixel
    // matrix multiply を避ける。
    const float4 worldPosition = mul(localPosition, transform.worldMatrix);
    const float4 viewPosition = mul(worldPosition, g_viewMatrix);
    const float3 worldNormal =
        normalize(mul(float4(localNormal, 0.0f), transform.normalMatrix).xyz);

    output.position = mul(viewPosition, g_projectionMatrix);
    output.worldPosition = worldPosition.xyz;
    output.viewPosition = viewPosition.xyz;
    output.worldNormal = worldNormal;
    return output;
}

uint compute_depth_slice(float viewZ)
{
    // Cluster grid の Z slice は logarithmic。near/far と逆数 log は
    // root constants で渡し、PS で projection matrix から復元しない。
    const uint depthSliceCount = max(g_clusterDepthSliceCount, 1u);
    const float safeNearZ = max(g_clusterNearZ, 0.0001f);
    const float safeFarZ = max(g_clusterFarZ, safeNearZ + 0.0001f);
    const float safeViewZ = clamp(viewZ, safeNearZ, safeFarZ);
    const float slice =
        log(safeViewZ / safeNearZ) *
        max(g_clusterInvLogFarNear, 0.0001f) *
        (float)depthSliceCount;
    return min((uint)slice, depthSliceCount - 1u);
}

uint compute_cluster_index(float4 screenPosition, float viewZ)
{
    // screen x/y と view-space z から cluster id を求める。
    // ここで得た id が ClusterLightRangeBuffer の index になる。
    const uint tileCountX = max(g_clusterCountX, 1u);
    const uint tileCountY = max(g_clusterCountY, 1u);
    const float2 screenSize =
        max(float2((float)g_screenWidth, (float)g_screenHeight),
            float2(1.0f, 1.0f));

    const uint tileX = min((uint)(screenPosition.x / screenSize.x *
                                  (float)tileCountX),
                           tileCountX - 1u);
    const uint tileY = min((uint)(screenPosition.y / screenSize.y *
                                  (float)tileCountY),
                           tileCountY - 1u);

    const uint sliceZ = compute_depth_slice(viewZ);

    return (sliceZ * tileCountY + tileY) * tileCountX + tileX;
}

float3 evaluate_lighting(float4 screenPosition, float3 worldPosition,
                         float3 viewPosition, float3 worldNormal)
{
    float3 lighting =
        g_lightFrame.ambientColorIntensity.rgb *
        g_lightFrame.ambientColorIntensity.a;

    const uint directionalCount = min(g_lightFrame.directionalLightCount, 1u);
    for (uint lightIndex = 0; lightIndex < directionalCount; ++lightIndex)
    {
        const DirectionalLight light = g_directionalLights[lightIndex];
        const float3 lightDirection = -normalize(light.directionIntensity.xyz);
        const float diffuse = saturate(dot(worldNormal, lightDirection));
        lighting +=
            light.color.rgb *
            light.directionIntensity.w *
            diffuse;
    }

    const uint clusterIndex =
        compute_cluster_index(screenPosition, viewPosition.z);
    const ClusterLightRange range = g_clusterLightRanges[clusterIndex];

    // cluster に割り当てられた point light だけを評価する。
    // 全 light 走査を避けるのが Clustered Forward の主目的。
    for (uint rangeIndex = 0; rangeIndex < range.count; ++rangeIndex)
    {
        const uint lightIndex =
            g_clusterLightIndices[range.offset + rangeIndex];
        const PointLight light = g_pointLights[lightIndex];
        const float3 toLight = light.positionRange.xyz - worldPosition;
        const float distance = length(toLight);
        const float range = max(light.positionRange.w, 0.001f);
        const float3 lightDirection =
            distance > 0.0001f ? toLight / distance : float3(0.0f, 1.0f, 0.0f);
        const float attenuation = pow(saturate(1.0f - distance / range), 2.0f);
        const float diffuse = saturate(dot(worldNormal, lightDirection));

        lighting +=
            light.colorIntensity.rgb *
            light.colorIntensity.w *
            attenuation *
            diffuse;
    }

    return lighting;
}

float4 ps_main(VsOutput input) : SV_Target0
{
    const Material material = g_materials[input.materialId];
    const float3 normal = normalize(input.worldNormal);
    const float3 lighting =
        max(evaluate_lighting(input.position, input.worldPosition,
                              input.viewPosition, normal),
            0.0f);
    return float4(material.color.rgb * lighting, material.color.a);
}
