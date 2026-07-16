// Packs visible RenderObject entries into per-batch instance lists.
// The batch key is meshId/materialId/depthBin, and later indirect commands draw
// contiguous ranges from this list.

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
RWStructuredBuffer<uint> g_renderObjectIndices : register(u0);
RWByteAddressBuffer g_batchWriteOffsets : register(u1);

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

cbuffer DrawInstanceParam : register(b3)
{
    uint g_maxDrawInstanceCount;
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

    // Lanes with the same batchId in a wave share one atomic add.
    // This reduces counter contention compared with per-object atomics.
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

        uint waveBaseOffset = 0u;
        if (WaveGetLaneIndex() == leaderLane)
        {
            g_batchWriteOffsets.InterlockedAdd(
                leaderBatchId * 4u, matchingCount, waveBaseOffset);
        }
        waveBaseOffset = WaveReadLaneAt(waveBaseOffset, leaderLane);

        if (matching)
        {
            // Write this object index into the batch range reserved by prefix sum.
            const uint drawInstanceIndex =
                waveBaseOffset + WavePrefixCountBits(matching);
            if (drawInstanceIndex < g_maxDrawInstanceCount)
            {
                g_renderObjectIndices[drawInstanceIndex] = objectIndex;
            }
        }

        remaining = remaining && !matching;
    }
}
