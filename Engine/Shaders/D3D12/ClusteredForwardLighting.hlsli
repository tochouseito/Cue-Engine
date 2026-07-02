#ifndef CUE_CLUSTERED_FORWARD_LIGHTING_TYPES_HLSLI
#define CUE_CLUSTERED_FORWARD_LIGHTING_TYPES_HLSLI

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
    uint hash;
    uint overflow;
};

#endif

#ifdef CUE_CLUSTERED_FORWARD_LIGHTING_IMPLEMENTATION
#ifndef CUE_CLUSTERED_FORWARD_LIGHTING_IMPL_HLSLI
#define CUE_CLUSTERED_FORWARD_LIGHTING_IMPL_HLSLI

uint cue_compute_depth_slice(float viewZ)
{
    const uint depthSliceCount = max(g_clusterDepthSliceCount, 1u);
    const float projectionA = g_projectionMatrix[2][2];
    const float projectionB = g_projectionMatrix[3][2];
    const float projectedNearZ =
        max(projectionB / (-1.0f - projectionA), 0.0001f);
    const float projectedFarZ =
        max(projectionB / (1.0f - projectionA), projectedNearZ + 0.0001f);
    const float safeNearZ = projectedNearZ;
    const float safeFarZ = projectedFarZ;
    const float invLogFarNear = rcp(max(log(safeFarZ / safeNearZ), 0.0001f));
    const float safeViewZ = clamp(viewZ, safeNearZ, safeFarZ);
    const float slice =
        log(safeViewZ / safeNearZ) *
        invLogFarNear *
        (float)depthSliceCount;
    return min((uint)slice, depthSliceCount - 1u);
}

uint cue_compute_cluster_index(float2 screenPosition, float viewZ)
{
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

    const uint sliceZ = cue_compute_depth_slice(viewZ);
    return (sliceZ * tileCountY + tileY) * tileCountX + tileX;
}

float3 cue_evaluate_clustered_lighting(
    float2 screenPosition,
    float3 worldPosition,
    float3 viewPosition,
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
        lighting += light.color.rgb * light.directionIntensity.w * diffuse;
    }

    const uint clusterIndex =
        cue_compute_cluster_index(screenPosition, viewPosition.z);
    const ClusterLightRange range = g_clusterLightRanges[clusterIndex];

    for (uint rangeIndex = 0; rangeIndex < range.count; ++rangeIndex)
    {
        const uint lightIndex =
            g_clusterLightIndices[range.offset + rangeIndex];
        const PointLight light = g_pointLights[lightIndex];
        const float3 toLight = light.positionRange.xyz - worldPosition;
        const float distance = length(toLight);
        const float lightRange = max(light.positionRange.w, 0.001f);
        const float3 lightDirection =
            distance > 0.0001f ? toLight / distance : float3(0.0f, 1.0f, 0.0f);
        const float attenuation =
            pow(saturate(1.0f - distance / lightRange), 2.0f);
        const float diffuse = saturate(dot(worldNormal, lightDirection));

        lighting +=
            light.colorIntensity.rgb *
            light.colorIntensity.w *
            attenuation *
            diffuse;
    }

    return lighting;
}

#endif
#endif
