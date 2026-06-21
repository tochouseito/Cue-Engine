// Visible RenderObject を batch key ごとに数える pass。
// ここでは instance list にはまだ書かず、PrefixSumPass が必要な範囲を
// 計算できるよう batch ごとの count だけを作る。

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

cbuffer RefinedVisibilityParam : register(b3)
{
    uint g_useRefinedVisibility;
};

cbuffer DrawPathParam : register(b4)
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
        active = g_objectDrawPath[objectIndex] == 1u;
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

    // wave 内の同一 batch をまとめて加算し、BatchKey 生成の O(N scan) を避ける。
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

        // leader lane だけが atomic add する。matchingCount は同じ batchId の
        // lane 数なので、wave 内の複数 object を 1 回で数えられる。
        if (WaveGetLaneIndex() == leaderLane)
        {
            uint previousCount = 0u;
            g_batchObjectCounts.InterlockedAdd(
                leaderBatchId * 4u, matchingCount, previousCount);
        }

        remaining = remaining && !matching;
    }
}
