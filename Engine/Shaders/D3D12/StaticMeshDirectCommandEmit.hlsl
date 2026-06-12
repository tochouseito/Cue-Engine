// Emit one DrawIndexedInstanced command per visible render object.
// This keeps the final shading path identical while disabling draw batching.

struct MeshRange
{
    uint indexCount;
    uint startIndex;
    int baseVertex;
    uint padding;
};

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

struct IndirectCommand
{
    uint drawObjectStartIndex;
    uint indexCountPerInstance;
    uint instanceCount;
    uint startIndexLocation;
    int baseVertexLocation;
    uint startInstanceLocation;
};

StructuredBuffer<MeshRange> g_meshRanges : register(t0);
StructuredBuffer<RenderObject> g_renderObjects : register(t1);
ByteAddressBuffer g_renderObjectCount : register(t2);

RWStructuredBuffer<uint> g_renderObjectIndices : register(u0);
RWStructuredBuffer<IndirectCommand> g_indirectCommands : register(u1);
RWByteAddressBuffer g_indirectCommandCount : register(u2);

cbuffer MaxCommandCountParam : register(b0)
{
    uint g_maxCommandCount;
};

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint objectIndex = dispatchThreadId.x;
    const uint objectCount = min(g_renderObjectCount.Load(0), g_maxCommandCount);
    if (objectIndex >= objectCount)
    {
        return;
    }

    const RenderObject renderObject = g_renderObjects[objectIndex];
    const MeshRange meshRange = g_meshRanges[renderObject.meshId];
    if (meshRange.indexCount == 0u)
    {
        return;
    }

    uint commandIndex = 0u;
    g_indirectCommandCount.InterlockedAdd(0, 1u, commandIndex);
    if (commandIndex >= g_maxCommandCount)
    {
        return;
    }

    g_renderObjectIndices[commandIndex] = objectIndex;

    IndirectCommand indirectCommand;
    indirectCommand.drawObjectStartIndex = commandIndex;
    indirectCommand.indexCountPerInstance = meshRange.indexCount;
    indirectCommand.instanceCount = 1u;
    indirectCommand.startIndexLocation = meshRange.startIndex;
    indirectCommand.baseVertexLocation = meshRange.baseVertex;
    indirectCommand.startInstanceLocation = 0u;
    g_indirectCommands[commandIndex] = indirectCommand;
}
