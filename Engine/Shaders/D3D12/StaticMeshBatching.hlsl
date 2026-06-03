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

cbuffer BatchingParam : register(b0)
{
    uint g_bucketCapacity;
};

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint bucketIndex = dispatchThreadId.x;
    if (bucketIndex >= 4u)
    {
        return;
    }

    const uint visibleCount = g_renderObjectCount.Load(bucketIndex * 4u);
    const uint instanceCount = min(visibleCount, g_bucketCapacity);
    if (instanceCount == 0u)
    {
        return;
    }

    const uint objectIndex = bucketIndex * g_bucketCapacity;
    const RenderObject objectInfo = g_renderObjects[objectIndex];
    const MeshRange meshRange = g_meshRanges[objectInfo.meshId];

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
