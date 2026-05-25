#include "DrawCommon.hlsli"

cbuffer DispatchParam : register(b0)
{
    uint g_objectCount;
};

StructuredBuffer<RenderableInfo> g_renderableInfos : register(t0);
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
    RenderableInfo renderableInfo = g_renderableInfos[objectId];
    if(renderableInfo.visible == 0)
    {
        return;
    }

    uint dstIndex = 0;
    g_renderObjectCount.InterlockedAdd(0, 1, dstIndex);

    RenderObject renderObject;
    renderObject.id = renderableInfo.id;
    renderObject.meshId = renderableInfo.meshId;
    renderObject.transformId = renderableInfo.transformId;
    renderObject.materialId = renderableInfo.materialId;
    renderObject.castsShadow = renderableInfo.castsShadow;
    renderObject.receivesShadow = renderableInfo.receivesShadow;
    renderObject.shadowCasterMode = renderableInfo.shadowCasterMode;
    renderObject.skinPaletteOffset = renderableInfo.skinPaletteOffset;
    renderObject.skinPaletteCount = renderableInfo.skinPaletteCount;
    g_renderObjects[dstIndex] = renderObject;
}
