#include "DrawCommon.hlsli"

StructuredBuffer<RenderObject> g_renderObjects : register(t0);
ByteAddressBuffer g_renderObjectCount : register(t1);
ByteAddressBuffer g_bucketOffsets : register(t2);
RWByteAddressBuffer g_bucketCursor : register(u0);
RWStructuredBuffer<RenderObject> g_sortedRenderObjects : register(u1);

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint objectCount = g_renderObjectCount.Load(0);
    const uint objectIndex = dispatchThreadId.x;
    if (objectIndex >= objectCount)
    {
        return;
    }

    const RenderObject renderObject = g_renderObjects[objectIndex];
    const uint byteOffset = renderObject.meshId * 4;
    const uint bucketOffset = g_bucketOffsets.Load(byteOffset);
    uint localOffset = 0;
    g_bucketCursor.InterlockedAdd(byteOffset, 1, localOffset);
    g_sortedRenderObjects[bucketOffset + localOffset] = renderObject;
}
