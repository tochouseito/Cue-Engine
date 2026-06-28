// Builds one indirect draw command per visible Forward fallback object.

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

struct MeshRange
{
    uint indexCount;
    uint startIndex;
    int baseVertex;
    uint firstMeshlet;
    uint meshletCount;
    uint rangeStartIndex;
    uint rangeIndexCount;
    uint visibilityTriangleStart;
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

static const uint kRenderObjectFlagForwardFallback = 1u << 0u;

StructuredBuffer<RenderObject> g_renderObjects : register(t0);
ByteAddressBuffer g_renderObjectCount : register(t1);
StructuredBuffer<MeshRange> g_meshRanges : register(t2);

RWStructuredBuffer<IndirectCommand> g_indirectCommands : register(u0);
RWByteAddressBuffer g_indirectCommandCount : register(u1);
RWStructuredBuffer<uint> g_renderObjectIndices : register(u2);

cbuffer MaxCommandCountParam : register(b0)
{
    uint g_maxCommandCount;
};

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint objectIndex = dispatchThreadId.x;
    const uint visibleObjectCount = g_renderObjectCount.Load(0);
    if (objectIndex >= visibleObjectCount)
    {
        return;
    }

    const RenderObject renderObject = g_renderObjects[objectIndex];
    if ((renderObject.padding & kRenderObjectFlagForwardFallback) == 0u)
    {
        return;
    }

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

    IndirectCommand command;
    command.drawObjectStartIndex = commandIndex;
    command.indexCountPerInstance = meshRange.indexCount;
    command.instanceCount = 1u;
    command.startIndexLocation = meshRange.startIndex;
    command.baseVertexLocation = meshRange.baseVertex;
    command.startInstanceLocation = 0u;
    g_indirectCommands[commandIndex] = command;
}
