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
};

struct Transform
{
    row_major float4x4 worldMatrix;
    row_major float4x4 normalMatrix;
};

struct MeshRange
{
    uint indexCount;
    uint startIndex;
    int baseVertex;
    uint padding;
};

struct IndirectCommand
{
    uint drawObjectStartIndex;
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
RWStructuredBuffer<uint> g_renderObjectIndices : register(u2);
RWByteAddressBuffer g_renderObjectIndexCount : register(u3);

cbuffer BatchingParam : register(b0)
{
    uint g_maxObjectCount;
};

bool has_previous_same_batch_key(uint objectIndex, uint meshId)
{
    for (uint previousIndex = 0; previousIndex < objectIndex; ++previousIndex)
    {
        if (g_renderObjects[previousIndex].meshId == meshId)
        {
            return true;
        }
    }

    return false;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint objectIndex = dispatchThreadId.x;
    const uint visibleObjectCount = min(g_renderObjectCount.Load(0), g_maxObjectCount);
    if (objectIndex >= visibleObjectCount)
    {
        return;
    }

    const RenderObject objectInfo = g_renderObjects[objectIndex];
    if (has_previous_same_batch_key(objectIndex, objectInfo.meshId))
    {
        return;
    }

    uint instanceCount = 0;
    for (uint sourceIndex = 0; sourceIndex < visibleObjectCount; ++sourceIndex)
    {
        if (g_renderObjects[sourceIndex].meshId == objectInfo.meshId)
        {
            ++instanceCount;
        }
    }

    if (instanceCount == 0u)
    {
        return;
    }

    uint indexStart = 0;
    g_renderObjectIndexCount.InterlockedAdd(0, instanceCount, indexStart);
    if (indexStart >= g_maxObjectCount)
    {
        return;
    }

    const uint writableInstanceCount =
        min(instanceCount, g_maxObjectCount - indexStart);
    uint localIndex = 0;
    for (uint writeSourceIndex = 0;
         writeSourceIndex < visibleObjectCount && localIndex < writableInstanceCount;
         ++writeSourceIndex)
    {
        if (g_renderObjects[writeSourceIndex].meshId == objectInfo.meshId)
        {
            g_renderObjectIndices[indexStart + localIndex] = writeSourceIndex;
            ++localIndex;
        }
    }

    const MeshRange meshRange = g_meshRanges[objectInfo.meshId];

    uint dstIndex = 0;
    g_indirectCommandCount.InterlockedAdd(0, 1, dstIndex);

    IndirectCommand indirectCommand;
    indirectCommand.drawObjectStartIndex = indexStart;
    indirectCommand.indexCountPerInstance = meshRange.indexCount;
    indirectCommand.instanceCount = writableInstanceCount;
    indirectCommand.startIndexLocation = meshRange.startIndex;
    indirectCommand.baseVertexLocation = meshRange.baseVertex;
    indirectCommand.startInstanceLocation = 0;
    g_indirectCommands[dstIndex] = indirectCommand;
}
