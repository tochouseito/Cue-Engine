// Visible RenderObject を batch key ごとの instance list へ詰める pass。
// batch key は meshId/materialId/depthBin で、後段の indirect command は
// この list の連続範囲を instance 描画する。

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
StructuredBuffer<uint> g_refinedVisibility : register(t2);
StructuredBuffer<uint> g_objectDrawPath : register(t3);
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

cbuffer RefinedVisibilityParam : register(b4)
{
    uint g_useRefinedVisibility;
};

cbuffer DrawPathParam : register(b5)
{
    uint g_useDrawPath;
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
    if (active && g_useRefinedVisibility != 0u)
    {
        active = g_refinedVisibility[objectIndex] != 0u;
    }
    if (active && g_useDrawPath != 0u)
    {
        const uint drawPath = g_objectDrawPath[objectIndex];
        active = drawPath == 1u || drawPath == 3u;
    }
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

    // wave 内で同じ batchId の lane をまとめて 1 回だけ atomic add する。
    // object ごとに atomic するより counter contention を抑えられる。
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
            // prefix sum で確保済みの batch 範囲へ、この object の index を書く。
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
