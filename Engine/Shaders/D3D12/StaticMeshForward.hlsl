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
    nointerpolation uint lodIndex : TEXCOORD2;
    nointerpolation uint drawFlags : TEXCOORD3;
    nointerpolation uint depthBin : TEXCOORD4;
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
ByteAddressBuffer g_hizDepth : register(t9);

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

cbuffer DebugViewModeParam : register(b11)
{
    uint g_debugViewMode;
};

cbuffer HiZTileSizeParam : register(b12)
{
    uint g_hizTileSize;
};

VsOutput vs_main(VsInput input, uint instanceId : SV_InstanceID)
{
    const uint renderObjectIndex =
        g_renderObjectIndices[g_drawObjectIndex.drawObjectIndex + instanceId];

    // 描画対象情報を取得する
    const RenderObject renderObject = g_renderObjects[renderObjectIndex];
    const Transform transform = g_transforms[renderObject.transformId];

    VsOutput output;
    output.texcoord = input.texcoord;
    output.materialId = renderObject.materialId;
    output.lodIndex = renderObject.padding;
    output.drawFlags = renderObject.drawFlags;
    output.depthBin = renderObject.depthBin;

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

float3 heat_color(float value)
{
    const float v = saturate(value);
    return saturate(float3(v * 2.0f - 0.15f,
                           1.0f - abs(v - 0.5f) * 2.0f,
                           1.15f - v * 2.0f));
}

float3 lod_debug_color(uint lodIndex)
{
    if (lodIndex == 0u)
    {
        return float3(1.0f, 0.12f, 0.08f);
    }
    if (lodIndex == 1u)
    {
        return float3(0.1f, 0.95f, 0.2f);
    }
    if (lodIndex == 2u)
    {
        return float3(0.15f, 0.35f, 1.0f);
    }
    if (lodIndex == 3u)
    {
        return float3(1.0f, 0.9f, 0.08f);
    }
    return float3(1.0f, 0.15f, 0.95f);
}

float3 depth_debug_color(float viewZ)
{
    const float viewDepth = max(abs(viewZ), g_clusterNearZ);
    const float depth01 =
        saturate(log(viewDepth / g_clusterNearZ) *
                 max(g_clusterInvLogFarNear, 0.0001f));
    return float3(depth01, depth01, depth01);
}

float3 hiz_debug_color(float4 screenPosition)
{
    const uint tileSize = max(g_hizTileSize, 1u);
    const uint tileCountX = max((g_screenWidth + tileSize - 1u) / tileSize, 1u);
    const uint tileCountY =
        max((g_screenHeight + tileSize - 1u) / tileSize, 1u);
    const uint tileX = min((uint)(screenPosition.x) / tileSize, tileCountX - 1u);
    const uint tileY = min((uint)(screenPosition.y) / tileSize, tileCountY - 1u);
    const uint tileIndex = tileY * tileCountX + tileX;
    const uint quantizedDepth = g_hizDepth.Load(tileIndex * 4u);
    const float depth01 = (float)quantizedDepth * (1.0f / 4294967295.0f);
    return heat_color(depth01);
}

float3 cluster_debug_color(float4 screenPosition, float viewZ)
{
    const uint clusterIndex = compute_cluster_index(screenPosition, viewZ);
    const ClusterLightRange range = g_clusterLightRanges[clusterIndex];
    if (range.overflow != 0u)
    {
        return float3(1.0f, 0.0f, 1.0f);
    }

    const float load01 = saturate((float)range.count / 16.0f);
    const float hash01 = (float)((range.hash ^ clusterIndex) & 255u) / 255.0f;
    return lerp(float3(hash01 * 0.25f, 0.12f, 0.35f),
                heat_color(load01),
                0.8f);
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
    if (g_debugViewMode == 1u)
    {
        return float4(lod_debug_color(input.lodIndex), 1.0f);
    }
    if (g_debugViewMode == 2u)
    {
        const float dither = (float)(((uint)input.position.x ^
                                      ((uint)input.position.y * 17u)) & 1u);
        return float4(0.04f + dither * 0.06f, 0.85f, 0.25f, 1.0f);
    }
    if (g_debugViewMode == 3u)
    {
        return float4(depth_debug_color(input.viewPosition.z), 1.0f);
    }
    if (g_debugViewMode == 4u)
    {
        return float4(hiz_debug_color(input.position), 1.0f);
    }
    if (g_debugViewMode == 5u)
    {
        const bool usesProxy = (input.drawFlags & 2u) != 0u;
        return usesProxy ? float4(1.0f, 0.55f, 0.05f, 1.0f)
                         : float4(0.08f, 0.18f, 0.35f, 1.0f);
    }
    if (g_debugViewMode == 6u)
    {
        return float4(cluster_debug_color(input.position,
                                          input.viewPosition.z),
                      1.0f);
    }

    const Material material = g_materials[input.materialId];
    const float3 normal = normalize(input.worldNormal);
    const float3 lighting =
        max(evaluate_lighting(input.position, input.worldPosition,
                              input.viewPosition, normal),
            0.0f);
    return float4(material.color.rgb * lighting, material.color.a);
}
