// Count visible objects per bucket before scatter to reserve stable output ranges on the GPU.

#include "DrawCommon.hlsli"

StructuredBuffer<RenderObject> g_renderObjects : register(t0);
ByteAddressBuffer g_renderObjectCount : register(t1);
RWByteAddressBuffer g_bucketCounts : register(u0);

[numthreads(64, 1, 1)]
// Compute entry point runs one logical item per dispatch thread to avoid CPU-side iteration.
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint objectCount = g_renderObjectCount.Load(0);
    const uint objectIndex = dispatchThreadId.x;
    if (objectIndex >= objectCount)
    {
        return;
    }

    const uint meshId = g_renderObjects[objectIndex].meshId;
    uint previousValue = 0;
    g_bucketCounts.InterlockedAdd(meshId * 4, 1, previousValue);
}
