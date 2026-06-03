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

uint count_instances(uint startObjectIndex, uint objectCount, uint meshId)
{
    uint instanceCount = 1;
    for (uint objectIndex = startObjectIndex + 1;
        objectIndex < objectCount;
        ++objectIndex)
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
    const uint objectCount = g_renderObjectCount.Load(0);
    const uint objectIndex = dispatchThreadId.x;
    if (objectIndex >= objectCount)
    {
        return;
    }

    const RenderObject objectInfo = g_renderObjects[objectIndex];
    if (objectIndex > 0 &&
        g_renderObjects[objectIndex - 1].meshId == objectInfo.meshId)
    {
        return;
    }

    const MeshRange meshRange = g_meshRanges[objectInfo.meshId];
    const uint instanceCount =
        count_instances(objectIndex, objectCount, objectInfo.meshId);

    uint dstIndex = 0;
    g_indirectCommandCount.InterlockedAdd(0, 1, dstIndex);

    IndirectCommand indirectCommand;
    indirectCommand.drawObjectStartIndex = objectIndex;
    indirectCommand.indexCountPerInstance = meshRange.indexCount;
    indirectCommand.instanceCount = instanceCount;
    indirectCommand.startIndexLocation = meshRange.startIndex;
    indirectCommand.baseVertexLocation = meshRange.baseVertex;
    indirectCommand.startInstanceLocation = 0;
    g_indirectCommands[dstIndex] = indirectCommand;
}
