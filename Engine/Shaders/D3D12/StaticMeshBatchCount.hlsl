// Counts visible RenderObject entries per batch key.
// This pass only writes per-batch counts so PrefixSumPass can compute the
// instance-list ranges.

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

StructuredBuffer<RenderObject> g_renderObjects : register(t0);
ByteAddressBuffer g_renderObjectCount : register(t1);
RWByteAddressBuffer g_batchObjectCounts : register(u0);

cbuffer BatchParam : register(b0)
{
    uint g_maxMeshCount;
};

cbuffer MaterialParam : register(b1)
{
    uint g_maxMaterialCount;
};

cbuffer DepthBinParam : register(b2)
{
    uint g_depthBinCount;
};

uint first_active_lane(uint4 mask)
{
    if (mask.x != 0u)
    {
        return (uint)firstbitlow(mask.x);
    }
    if (mask.y != 0u)
    {
        return 32u + (uint)firstbitlow(mask.y);
    }
    if (mask.z != 0u)
    {
        return 64u + (uint)firstbitlow(mask.z);
    }
    return 96u + (uint)firstbitlow(mask.w);
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint objectIndex = dispatchThreadId.x;
    const uint visibleObjectCount = g_renderObjectCount.Load(0);
    bool active = objectIndex < visibleObjectCount;
    uint batchId = 0u;
    if (active)
    {
        const RenderObject renderObject = g_renderObjects[objectIndex];
        active =
            renderObject.meshId < g_maxMeshCount &&
            renderObject.materialId < g_maxMaterialCount &&
            renderObject.depthBin < g_depthBinCount;

        if (active)
        {
            batchId =
                (renderObject.meshId * g_maxMaterialCount +
                    renderObject.materialId) *
                    g_depthBinCount +
                renderObject.depthBin;
        }
    }

    // Batch matches inside a wave are accumulated together to avoid an O(N) scan.
    bool remaining = active;
    for (;;)
    {
        const uint4 remainingMask = WaveActiveBallot(remaining);
        if (!any(remainingMask != 0u))
        {
            return;
        }

        const uint leaderLane = first_active_lane(remainingMask);
        const uint leaderBatchId = WaveReadLaneAt(batchId, leaderLane);
        const bool matching = remaining && batchId == leaderBatchId;
        const uint matchingCount = WaveActiveCountBits(matching);

        // Only the leader lane performs the atomic add.
        // matchingCount covers every lane with the same batchId in this wave.
        if (WaveGetLaneIndex() == leaderLane)
        {
            uint previousCount = 0u;
            g_batchObjectCounts.InterlockedAdd(
                leaderBatchId * 4u, matchingCount, previousCount);
        }

        remaining = remaining && !matching;
    }
}
