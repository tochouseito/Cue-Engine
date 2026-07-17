// Light-centric clustered point-light assignment.
//
// BuildCandidatesCS walks the small fixed cluster axes for each light, then
// appends the light only to clusters whose AABB intersects the light sphere.
// FinalizeClustersCS publishes the fixed per-cluster ranges consumed by the
// forward pixel shader.

#define CANDIDATE_GROUP_SIZE 64
#define FINALIZE_GROUP_SIZE 128

struct ViewPointLight
{
    float4 positionRadius;
    float4 colorIntensity;
    uint sourceIndex;
    uint padding0;
    uint padding1;
    uint padding2;
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
    uint hash;
    uint overflow;
};

StructuredBuffer<Cluster> g_clusters : register(t0);
StructuredBuffer<ViewPointLight> g_viewPointLights : register(t1);
ByteAddressBuffer g_candidateCountsRead : register(t2);

RWByteAddressBuffer g_candidateCounts : register(u0);
RWStructuredBuffer<uint> g_clusterLightIndices : register(u1);
RWStructuredBuffer<ClusterLightRange> g_clusterLightRanges : register(u2);

cbuffer ClusterCountParam : register(b0)
{
    uint g_clusterCount;
};

cbuffer PointLightCountParam : register(b1)
{
    uint g_pointLightCount;
};

cbuffer TileCountXParam : register(b2)
{
    uint g_tileCountX;
};

cbuffer TileCountYParam : register(b3)
{
    uint g_tileCountY;
};

cbuffer DepthSliceCountParam : register(b4)
{
    uint g_depthSliceCount;
};

cbuffer MaxLightsPerClusterParam : register(b5)
{
    uint g_maxLightsPerCluster;
};

uint cluster_index(uint tileX, uint tileY, uint sliceZ)
{
    return (sliceZ * g_tileCountY + tileY) * g_tileCountX + tileX;
}

bool sphere_intersects_aabb(float3 center, float radius, float3 minPoint,
                            float3 maxPoint)
{
    const float3 closest = clamp(center, minPoint, maxPoint);
    const float3 delta = center - closest;
    return dot(delta, delta) <= radius * radius;
}

bool interval_intersects(float center, float radius, float minimum,
                         float maximum)
{
    return center + radius >= minimum && center - radius <= maximum;
}

[numthreads(CANDIDATE_GROUP_SIZE, 1, 1)]
void BuildCandidatesCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint lightIndex = dispatchThreadId.x;
    if (lightIndex >= g_pointLightCount || g_clusterCount == 0u ||
        g_tileCountX == 0u || g_tileCountY == 0u ||
        g_depthSliceCount == 0u || g_maxLightsPerCluster == 0u)
    {
        return;
    }

    const ViewPointLight light = g_viewPointLights[lightIndex];
    const float3 center = light.positionRadius.xyz;
    const float radius = light.positionRadius.w;

    // PreparePointLights writes radius zero for unused capacity slots.
    if (radius <= 0.0f)
    {
        return;
    }

    // Cluster AABB x bounds depend only on tileX/sliceZ and y bounds depend
    // only on tileY/sliceZ. Scan those two short axes first, then run the same
    // precise sphere/AABB test as the old cluster-centric implementation only
    // for the resulting rectangle.
    for (uint sliceZ = 0u; sliceZ < g_depthSliceCount; ++sliceZ)
    {
        const Cluster sliceCluster =
            g_clusters[cluster_index(0u, 0u, sliceZ)];
        if (!interval_intersects(center.z, radius,
                                 sliceCluster.minPoint.z,
                                 sliceCluster.maxPoint.z))
        {
            continue;
        }

        uint minTileX = g_tileCountX;
        uint maxTileX = 0u;
        for (uint tileX = 0u; tileX < g_tileCountX; ++tileX)
        {
            const Cluster axisCluster =
                g_clusters[cluster_index(tileX, 0u, sliceZ)];
            if (interval_intersects(center.x, radius,
                                    axisCluster.minPoint.x,
                                    axisCluster.maxPoint.x))
            {
                minTileX = min(minTileX, tileX);
                maxTileX = max(maxTileX, tileX);
            }
        }
        if (minTileX == g_tileCountX)
        {
            continue;
        }

        uint minTileY = g_tileCountY;
        uint maxTileY = 0u;
        for (uint tileY = 0u; tileY < g_tileCountY; ++tileY)
        {
            const Cluster axisCluster =
                g_clusters[cluster_index(0u, tileY, sliceZ)];
            if (interval_intersects(center.y, radius,
                                    axisCluster.minPoint.y,
                                    axisCluster.maxPoint.y))
            {
                minTileY = min(minTileY, tileY);
                maxTileY = max(maxTileY, tileY);
            }
        }
        if (minTileY == g_tileCountY)
        {
            continue;
        }

        for (uint tileY = minTileY; tileY <= maxTileY; ++tileY)
        {
            for (uint tileX = minTileX; tileX <= maxTileX; ++tileX)
            {
                const uint clusterIndex =
                    cluster_index(tileX, tileY, sliceZ);
                if (clusterIndex >= g_clusterCount)
                {
                    continue;
                }

                const Cluster cluster = g_clusters[clusterIndex];
                if (!sphere_intersects_aabb(center, radius,
                                            cluster.minPoint.xyz,
                                            cluster.maxPoint.xyz))
                {
                    continue;
                }

                uint candidateOffset = 0u;
                g_candidateCounts.InterlockedAdd(
                    clusterIndex * 4u, 1u, candidateOffset);
                if (candidateOffset < g_maxLightsPerCluster)
                {
                    const uint outputIndex =
                        clusterIndex * g_maxLightsPerCluster +
                        candidateOffset;
                    g_clusterLightIndices[outputIndex] = light.sourceIndex;
                }
            }
        }
    }
}

[numthreads(FINALIZE_GROUP_SIZE, 1, 1)]
void FinalizeClustersCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint clusterIndex = dispatchThreadId.x;
    if (clusterIndex >= g_clusterCount)
    {
        return;
    }

    const uint candidateCount =
        g_candidateCountsRead.Load(clusterIndex * 4u);
    const uint lightCount = min(candidateCount, g_maxLightsPerCluster);

    ClusterLightRange range;
    range.offset = clusterIndex * g_maxLightsPerCluster;
    range.count = lightCount;
    // Fixed ranges no longer need list-sharing fingerprints. Keeping a stable
    // cluster-derived value preserves the existing debug-view color variation.
    range.hash = clusterIndex * 16777619u;
    range.overflow = candidateCount > g_maxLightsPerCluster ? 1u : 0u;
    g_clusterLightRanges[clusterIndex] = range;

}
