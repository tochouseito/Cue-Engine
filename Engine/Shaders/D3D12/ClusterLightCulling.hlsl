// Optimized clustered point-light assignment.
// Uses cluster AABBs from BuildClusterGrid and view-space lights from
// PreparePointLights to build per-cluster light lists in a compact buffer.

#define GROUP_SIZE 128
#define MAX_LIGHTS_PER_CLUSTER 128

// Point light converted ahead of time into ViewPointLightBuffer.
// Cluster assignment compares against view-space AABBs, so only view-space
// position and radius are needed here.
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
    // Start offset of the light list inside ClusterLightIndexBuffer.
    // Clusters with identical light lists can share the same offset/count.
    uint offset;
    uint count;

    // Fingerprint for list-sharing candidates.
    // Hash alone is not enough in practice, so count/first/last light id narrow the match.
    uint hash;
    uint overflow;
};

// One thread handles one cluster, while the workgroup batch-loads lights into
// shared memory. This scratch space avoids rereading the same light from global
// memory once per cluster.
groupshared ViewPointLight s_lights[GROUP_SIZE];
groupshared uint s_hashes[GROUP_SIZE];
groupshared uint s_starts[GROUP_SIZE];
groupshared uint s_counts[GROUP_SIZE];
groupshared uint s_overflows[GROUP_SIZE];
groupshared uint s_firstLightIds[GROUP_SIZE];
groupshared uint s_lastLightIds[GROUP_SIZE];

StructuredBuffer<Cluster> g_clusters : register(t0);
StructuredBuffer<ViewPointLight> g_viewPointLights : register(t1);

RWStructuredBuffer<ClusterLightRange> g_clusterLightRanges : register(u0);
RWStructuredBuffer<uint> g_clusterLightIndices : register(u1);
RWByteAddressBuffer g_clusterLightItemCounter : register(u2);
RWByteAddressBuffer g_clusterLightStats : register(u3);

// Uint offsets into ClusterLightingStatsGpu.
// GPU-side aggregation exposes how compact the cluster assignment is in ImGui.
static const uint k_statClusterCount = 0u;
static const uint k_statActiveClusterCount = 4u;
static const uint k_statPointLightCount = 8u;
static const uint k_statTotalClusterItems = 12u;
static const uint k_statMaxLightsInCluster = 16u;
static const uint k_statOverflowClusterCount = 20u;
static const uint k_statEmptyClusterCount = 24u;
static const uint k_statReusedListCount = 28u;

cbuffer ClusterCountParam : register(b0)
{
    uint g_clusterCount;
};

cbuffer PointLightCountParam : register(b1)
{
    uint g_pointLightCount;
};

cbuffer MaxClusterLightItemsParam : register(b2)
{
    uint g_maxClusterLightItems;
};

cbuffer MaxLightsPerClusterParam : register(b3)
{
    uint g_maxLightsPerCluster;
};

bool sphere_intersects_aabb(float3 center, float radius, float3 minPoint,
                            float3 maxPoint)
{
    const float3 closest = clamp(center, minPoint, maxPoint);
    const float3 delta = center - closest;
    return dot(delta, delta) <= radius * radius;
}

uint hash_init()
{
    return 2166136261u;
}

uint hash_next(uint hash, uint value)
{
    hash ^= value;
    hash *= 16777619u;
    return hash;
}

[numthreads(GROUP_SIZE, 1, 1)]
void CSMain(uint3 groupId : SV_GroupID,
            uint3 groupThreadId : SV_GroupThreadID)
{
    // Each cluster builds an index list of point lights that affect it.
    // The forward PS evaluates only this list.
    const uint localId = groupThreadId.x;
    const uint clusterIndex = groupId.x * GROUP_SIZE + localId;

    // Build this cluster's light list in private storage.
    // Later, clusters with identical lists share one representative write to
    // ClusterLightIndexBuffer to reduce buffer usage.
    uint localLightIds[MAX_LIGHTS_PER_CLUSTER];
    uint localLightCount = 0u;
    uint overflow = 0u;
    uint hash = hash_init();
    uint firstLightId = 0xffffffffu;
    uint lastLightId = 0xffffffffu;

    Cluster cluster;
    bool validCluster = clusterIndex < g_clusterCount;
    if (validCluster)
    {
        cluster = g_clusters[clusterIndex];
    }
    else
    {
        cluster.minPoint = float4(0.0f, 0.0f, 0.0f, 0.0f);
        cluster.maxPoint = float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    for (uint batchStart = 0u; batchStart < g_pointLightCount;
         batchStart += GROUP_SIZE)
    {
        // The 128 threads in the workgroup load 128 lights into shared memory.
        // Each cluster thread intersects against the shared-memory lights.
        const uint lightIndex = batchStart + localId;
        if (lightIndex < g_pointLightCount)
        {
            s_lights[localId] = g_viewPointLights[lightIndex];
        }

        // Ensure the batch load completes before any thread reads s_lights.
        GroupMemoryBarrierWithGroupSync();

        const uint batchCount = min(GROUP_SIZE, g_pointLightCount - batchStart);
        if (validCluster)
        {
            for (uint i = 0u; i < batchCount; ++i)
            {
                const ViewPointLight light = s_lights[i];

                // Intersect the light sphere with the cluster AABB.
                // Only intersecting lights are evaluated by the pixel shader.
                if (sphere_intersects_aabb(light.positionRadius.xyz,
                                           light.positionRadius.w,
                                           cluster.minPoint.xyz,
                                           cluster.maxPoint.xyz))
                {
                    if (localLightCount < MAX_LIGHTS_PER_CLUSTER &&
                        localLightCount < g_maxLightsPerCluster)
                    {
                        localLightIds[localLightCount] = light.sourceIndex;
                        if (localLightCount == 0u)
                        {
                            firstLightId = light.sourceIndex;
                        }
                        lastLightId = light.sourceIndex;
                        ++localLightCount;
                        hash = hash_next(hash, light.sourceIndex);
                    }
                    else
                    {
                        // The cluster exceeded its light limit.
                        // Rendering continues with a clipped list, and stats expose the overflow.
                        overflow = 1u;
                    }
                }
            }
        }

        // Synchronize so the next batch write to s_lights cannot race with readers.
        GroupMemoryBarrierWithGroupSync();
    }

    // Fold count into the hash to reduce accidental matches between empty lists
    // and specific light sequences.
    hash = hash_next(hash, localLightCount);

    // Publish the fingerprint in shared memory so other threads can find matching lists.
    // The private light list itself is not readable by other threads.
    s_hashes[localId] = hash;
    s_starts[localId] = 0u;
    s_counts[localId] = localLightCount;
    s_overflows[localId] = overflow;
    s_firstLightIds[localId] = firstLightId;
    s_lastLightIds[localId] = lastLightId;

    GroupMemoryBarrierWithGroupSync();

    if (groupId.x == 0u && localId == 0u)
    {
        // Point light count is pass-wide, so only one thread writes it.
        g_clusterLightStats.Store(k_statPointLightCount, g_pointLightCount);
    }

    if (validCluster)
    {
        g_clusterLightStats.InterlockedAdd(k_statClusterCount, 1u);
        g_clusterLightStats.InterlockedAdd(k_statTotalClusterItems,
                                           localLightCount);
        if (localLightCount > 0u)
        {
            g_clusterLightStats.InterlockedAdd(k_statActiveClusterCount, 1u);
        }
        else
        {
            g_clusterLightStats.InterlockedAdd(k_statEmptyClusterCount, 1u);
        }
        uint previousMax = 0u;
        // Record the most populated cluster to judge cluster-grid granularity
        // and maxLightsPerCluster.
        g_clusterLightStats.InterlockedMax(k_statMaxLightsInCluster,
                                           localLightCount,
                                           previousMax);
    }

    uint firstLocalId = localId;
    if (validCluster)
    {
        [loop]
        for (uint i = 0u; i < GROUP_SIZE; ++i)
        {
            if (i > localId)
            {
                break;
            }

            if (s_hashes[i] == hash &&
                s_counts[i] == localLightCount &&
                s_firstLightIds[i] == firstLightId &&
                s_lastLightIds[i] == lastLightId)
            {
                // The first cluster with the same fingerprint becomes the representative.
                // This thread reuses the representative offset/count.
                firstLocalId = i;
                break;
            }
            else if (s_hashes[i] == hash)
            {
                // Hash matched but count/first/last did not. Kept for GPU
                // debugging by folding it into overflowClusterCount.
                g_clusterLightStats.InterlockedAdd(
                    k_statOverflowClusterCount, 1u);
            }
        }
    }

    if (validCluster && firstLocalId == localId)
    {
        uint start = 0u;

        // Only representative clusters append their list to the compact index buffer.
        // Non-representatives skip the write and reuse the same start/count.
        g_clusterLightItemCounter.InterlockedAdd(0u, localLightCount, start);

        if (start + localLightCount <= g_maxClusterLightItems)
        {
            for (uint i = 0u; i < localLightCount; ++i)
            {
                g_clusterLightIndices[start + i] = localLightIds[i];
            }

            s_starts[localId] = start;
            s_counts[localId] = localLightCount;
        }
        else
        {
            // The compact buffer is full.
            // Store an empty list to avoid out-of-range reads and expose capacity pressure in stats.
            s_starts[localId] = 0u;
            s_counts[localId] = 0u;
            s_overflows[localId] = 1u;
            g_clusterLightStats.InterlockedAdd(k_statOverflowClusterCount, 1u);
        }

        if (overflow != 0u)
        {
            g_clusterLightStats.InterlockedAdd(k_statOverflowClusterCount, 1u);
        }
    }
    else if (validCluster)
    {
        // Non-representative clusters reuse an existing light-list write.
        g_clusterLightStats.InterlockedAdd(k_statReusedListCount, 1u);
    }

    GroupMemoryBarrierWithGroupSync();

    if (validCluster)
    {
        // Mapping from cluster to the light list read by the forward pass.
        // offset/count identify the compact-buffer range; hash/overflow are debug data.
        ClusterLightRange range;
        range.offset = s_starts[firstLocalId];
        range.count = s_counts[firstLocalId];
        range.hash = hash;
        range.overflow = s_overflows[firstLocalId];

        g_clusterLightRanges[clusterIndex] = range;
    }
}
