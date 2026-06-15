// Collect per-frame draw statistics after final culling and command emission.

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
    uint lodIndex;
    float4 boundsCenterRadius;
};

struct DrawStats
{
    uint visibleObjects;
    uint culledObjects;
    uint indirectDrawCount;
    uint instanceCount;
    uint lod0Count;
    uint lod1Count;
    uint lod2Count;
    uint lod3Count;
    uint lod4Count;
    uint padding0;
    uint padding1;
    uint padding2;
};

StructuredBuffer<RenderObject> g_renderObjects : register(t0);
ByteAddressBuffer g_visibleObjectCount : register(t1);
ByteAddressBuffer g_indirectCommandCount : register(t2);
RWStructuredBuffer<DrawStats> g_drawStats : register(u0);

cbuffer TotalObjectCountParam : register(b0)
{
    uint g_totalObjectCount;
};

[numthreads(1, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    DrawStats stats;
    stats.visibleObjects = g_visibleObjectCount.Load(0);
    stats.culledObjects =
        g_totalObjectCount > stats.visibleObjects
            ? g_totalObjectCount - stats.visibleObjects
            : 0u;
    stats.indirectDrawCount = g_indirectCommandCount.Load(0);
    stats.instanceCount = stats.visibleObjects;
    stats.lod0Count = 0u;
    stats.lod1Count = 0u;
    stats.lod2Count = 0u;
    stats.lod3Count = 0u;
    stats.lod4Count = 0u;
    stats.padding0 = 0u;
    stats.padding1 = 0u;
    stats.padding2 = 0u;

    for (uint objectIndex = 0u; objectIndex < stats.visibleObjects; ++objectIndex)
    {
        const RenderObject renderObject = g_renderObjects[objectIndex];
        const uint lodIndex = min(renderObject.lodIndex, 4u);
        if (lodIndex == 0u)
        {
            ++stats.lod0Count;
        }
        else if (lodIndex == 1u)
        {
            ++stats.lod1Count;
        }
        else if (lodIndex == 2u)
        {
            ++stats.lod2Count;
        }
        else if (lodIndex == 3u)
        {
            ++stats.lod3Count;
        }
        else
        {
            ++stats.lod4Count;
        }
    }

    g_drawStats[0] = stats;
}
