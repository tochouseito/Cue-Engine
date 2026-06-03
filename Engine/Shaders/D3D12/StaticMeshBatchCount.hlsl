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

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint objectIndex = dispatchThreadId.x;
    const uint visibleObjectCount = g_renderObjectCount.Load(0);
    if (objectIndex >= visibleObjectCount)
    {
        return;
    }

    const RenderObject renderObject = g_renderObjects[objectIndex];
    if (renderObject.meshId >= g_maxMeshCount ||
        renderObject.materialId >= g_maxMaterialCount ||
        renderObject.depthBin >= g_depthBinCount)
    {
        return;
    }

    const uint batchId =
        (renderObject.meshId * g_maxMaterialCount + renderObject.materialId) *
            g_depthBinCount +
        renderObject.depthBin;
    uint previousCount = 0;
    g_batchObjectCounts.InterlockedAdd(batchId * 4u, 1u, previousCount);
}
