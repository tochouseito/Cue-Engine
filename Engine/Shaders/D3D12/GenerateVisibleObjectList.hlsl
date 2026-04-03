#include "DrawCommon.hlsli"

cbuffer DispatchParam : register(b0)
{
    uint g_objectCount;
};

StructuredBuffer<ObjectInfo> g_objectInfos : register(t0);
RWStructuredBuffer<RenderObject> g_renderObjects : register(u0);
RWByteAddressBuffer g_renderObjectCount : register(u1);

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint objectId = dispatchThreadId.x;
    if (objectId >= g_objectCount)
    {
        return;
    }
    ObjectInfo objectInfo = g_objectInfos[objectId];
    if(objectInfo.visible == 0)
    {
        return;
    }

    uint dstIndex = 0;
    g_renderObjectCount.InterlockedAdd(0, 1, dstIndex);

    RenderObject renderObject;
    renderObject.id = objectInfo.id;
    renderObject.meshId = objectInfo.meshId;
    renderObject.transformId = objectInfo.transformId;
    g_renderObjects[dstIndex] = renderObject;
}
