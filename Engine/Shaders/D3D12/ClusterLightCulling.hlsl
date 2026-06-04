struct LightFrame
{
    float4 ambientColorIntensity;
    uint directionalLightCount;
    uint pointLightCount;
    uint spotLightCount;
    uint padding;
};

struct PointLight
{
    float4 positionRange;
    float4 colorIntensity;
};

struct Cluster
{
    float4 minPoint;
    float4 maxPoint;
};

struct ClusterLightRange
{
    uint offset;
    uint count;
    uint padding0;
    uint padding1;
};

cbuffer ViewProjection : register(b0)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
};

ConstantBuffer<LightFrame> g_lightFrame : register(b2);

cbuffer ClusterCountParam : register(b3)
{
    uint g_clusterCount;
};

cbuffer TileCountXParam : register(b4)
{
    uint g_tileCountX;
};

cbuffer TileCountYParam : register(b5)
{
    uint g_tileCountY;
};

cbuffer DepthSliceCountParam : register(b6)
{
    uint g_depthSliceCount;
};

cbuffer MaxLightsPerClusterParam : register(b7)
{
    uint g_maxLightsPerCluster;
};

StructuredBuffer<Cluster> g_clusters : register(t0);
StructuredBuffer<PointLight> g_pointLights : register(t1);
RWStructuredBuffer<ClusterLightRange> g_clusterLightRanges : register(u0);
RWStructuredBuffer<uint> g_clusterLightIndices : register(u1);

bool sphere_intersects_aabb(float3 center, float radius, float3 minPoint,
                            float3 maxPoint)
{
    const float3 closest = clamp(center, minPoint, maxPoint);
    const float3 delta = center - closest;
    return dot(delta, delta) <= radius * radius;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint clusterIndex = dispatchThreadId.x;
    if (clusterIndex >= g_clusterCount)
    {
        return;
    }

    const Cluster cluster = g_clusters[clusterIndex];
    const uint baseOffset = clusterIndex * g_maxLightsPerCluster;
    uint writeCount = 0u;

    const uint pointCount = g_lightFrame.pointLightCount;
    for (uint lightIndex = 0u; lightIndex < pointCount; ++lightIndex)
    {
        const PointLight light = g_pointLights[lightIndex];
        const float3 viewPosition =
            mul(float4(light.positionRange.xyz, 1.0f), g_viewMatrix).xyz;
        const float radius = max(light.positionRange.w, 0.0f);

        if (sphere_intersects_aabb(viewPosition, radius,
                                   cluster.minPoint.xyz,
                                   cluster.maxPoint.xyz))
        {
            if (writeCount < g_maxLightsPerCluster)
            {
                g_clusterLightIndices[baseOffset + writeCount] = lightIndex;
                ++writeCount;
            }
        }
    }

    ClusterLightRange range;
    range.offset = baseOffset;
    range.count = writeCount;
    range.padding0 = 0u;
    range.padding1 = 0u;
    g_clusterLightRanges[clusterIndex] = range;
}
