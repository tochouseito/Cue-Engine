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
    uint offset;
    uint count;
    uint padding0;
    uint padding1;
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

cbuffer ClusterTileSizeParam : register(b3)
{
    uint g_clusterTileSize;
};

cbuffer ClusterTileCountXParam : register(b4)
{
    uint g_clusterTileCountX;
};

cbuffer ClusterTileCountYParam : register(b5)
{
    uint g_clusterTileCountY;
};

cbuffer ClusterDepthSliceCountParam : register(b6)
{
    uint g_clusterDepthSliceCount;
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

    if ((renderObject.drawFlags & 1u) != 0u)
    {
        const float4 worldCenter =
            float4(renderObject.boundsCenterRadius.xyz, 1.0f);
        const float objectScale = length(transform.worldMatrix[0].xyz);
        float4 viewPosition = mul(worldCenter, g_viewMatrix);
        viewPosition.xy += input.position.xy * objectScale;

        output.position = mul(viewPosition, g_projectionMatrix);
        output.worldPosition = worldCenter.xyz;
        output.worldNormal = float3(0.0f, 0.0f, 1.0f);
        return output;
    }

    // 頂点変換
    const float4 worldPosition = mul(input.position, transform.worldMatrix);
    const float3 worldNormal =
        normalize(mul(float4(input.normal, 0.0f), transform.normalMatrix).xyz);

    output.position = mul(mul(worldPosition, g_viewMatrix), g_projectionMatrix);
    output.worldPosition = worldPosition.xyz;
    output.worldNormal = worldNormal;
    return output;
}

void projection_near_far(out float nearZ, out float farZ)
{
    const float a = g_projectionMatrix[2][2];
    const float b = g_projectionMatrix[3][2];
    nearZ = max(b / (-1.0f - a), 0.0001f);
    farZ = max(b / (1.0f - a), nearZ + 0.0001f);
}

uint compute_cluster_index(float4 screenPosition, float3 worldPosition)
{
    const uint tileCountX = max(g_clusterTileCountX, 1u);
    const uint tileCountY = max(g_clusterTileCountY, 1u);
    const uint depthSliceCount = max(g_clusterDepthSliceCount, 1u);
    const uint tileSize = max(g_clusterTileSize, 1u);

    const uint tileX = min((uint)(screenPosition.x / (float)tileSize),
                           tileCountX - 1u);
    const uint tileY = min((uint)(screenPosition.y / (float)tileSize),
                           tileCountY - 1u);

    float nearZ = 0.0f;
    float farZ = 0.0f;
    projection_near_far(nearZ, farZ);
    const float viewZ = mul(float4(worldPosition, 1.0f), g_viewMatrix).z;
    const float normalizedDepth =
        saturate((viewZ - nearZ) / max(farZ - nearZ, 0.0001f));
    const uint sliceZ =
        min((uint)(normalizedDepth * (float)depthSliceCount),
            depthSliceCount - 1u);

    return (sliceZ * tileCountY + tileY) * tileCountX + tileX;
}

float3 evaluate_lighting(float4 screenPosition, float3 worldPosition,
                         float3 worldNormal)
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

    const uint clusterIndex = compute_cluster_index(screenPosition,
                                                    worldPosition);
    const ClusterLightRange range = g_clusterLightRanges[clusterIndex];
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
        max(evaluate_lighting(input.position, input.worldPosition, normal),
            0.0f);
    return float4(material.color.rgb * lighting, material.color.a);
}
