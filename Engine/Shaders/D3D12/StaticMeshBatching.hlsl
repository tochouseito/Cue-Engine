#include "DrawCommon.hlsli"

struct Transform
{
    float4x4 worldMatrix;
};

struct MeshRange
{
    uint indexCount;
    uint startIndex;
    int baseVertex;
};

cbuffer DispatchParam : register(b0)
{
    uint g_objectCount;
};

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

RWStructuredBuffer<IndirectCommand> g_indirectCommands : register(u0);
RWByteAddressBuffer g_indirectCommandCount : register(u1);

uint count_instances(uint startObjectIndex, uint meshId)
{
    uint instanceCount = 1;

    for (uint objectIndex = startObjectIndex + 1; objectIndex < g_objectCount; ++objectIndex)
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
    uint objectId = dispatchThreadId.x;
    if (objectId >= g_objectCount)
    {
        return;
    }

    RenderObject objectInfo = g_renderObjects[objectId];
    if (objectId > 0 && g_renderObjects[objectId - 1].meshId == objectInfo.meshId)
    {
        return;
    }

    MeshRange meshRange = g_meshRanges[objectInfo.meshId];
    uint instanceCount = count_instances(objectId, objectInfo.meshId);

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
