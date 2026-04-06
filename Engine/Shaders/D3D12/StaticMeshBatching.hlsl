#include "DrawCommon.hlsli"

struct IndirectCommand
{
    uint indexCountPerInstance;
    uint instanceCount;
    uint startIndexLocation;
    int baseVertexLocation;
    uint startInstanceLocation;
};

StructuredBuffer<RenderObject> g_renderObjects : register(t0);
StructuredBuffer<Transform> g_transforms : register(t1);
StructuredBuffer<MeshRange> g_meshRanges : register(t2);
ByteAddressBuffer g_renderObjectCount : register(t3);

RWStructuredBuffer<IndirectCommand> g_indirectCommands : register(u0);
RWByteAddressBuffer g_indirectCommandCount : register(u1);

uint count_instances(uint startObjectIndex, uint objectCount, uint meshId)
{
    uint instanceCount = 1;

    for (uint objectIndex = startObjectIndex + 1; objectIndex < objectCount; ++objectIndex)
    {
        if (g_renderObjects[objectIndex].meshId != meshId)
        {
            break;
        }

        ++instanceCount;
    }

    return instanceCount;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint objectCount = g_renderObjectCount.Load(0);
    uint objectId = dispatchThreadId.x;
    if (objectId >= objectCount)
    {
        return;
    }

    RenderObject objectInfo = g_renderObjects[objectId];
    if (objectId > 0 && g_renderObjects[objectId - 1].meshId == objectInfo.meshId)
    {
        return;
    }

    MeshRange meshRange = g_meshRanges[objectInfo.meshId];
    uint instanceCount = count_instances(objectId, objectCount, objectInfo.meshId);

    uint dstIndex = 0;
    g_indirectCommandCount.InterlockedAdd(0, 1, dstIndex);

    IndirectCommand indirectCommand;
    indirectCommand.indexCountPerInstance = meshRange.indexCount;
    indirectCommand.instanceCount = instanceCount;
    indirectCommand.startIndexLocation = meshRange.startIndex;
    indirectCommand.baseVertexLocation = meshRange.baseVertex;
    indirectCommand.startInstanceLocation = objectId;
    g_indirectCommands[dstIndex] = indirectCommand;
}
