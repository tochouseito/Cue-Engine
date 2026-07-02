// Build per-batch instance ranges and compact indirect draw commands.

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
    uint primitiveBase;
    uint indexCountPerInstance;
    uint instanceCount;
    uint startIndexLocation;
    int baseVertexLocation;
    uint startInstanceLocation;
};

ByteAddressBuffer g_batchObjectCounts : register(t0);
StructuredBuffer<MeshRange> g_meshRanges : register(t1);

RWByteAddressBuffer g_batchObjectStarts : register(u0);
RWByteAddressBuffer g_batchWriteOffsets : register(u1);
RWStructuredBuffer<IndirectCommand> g_indirectCommands : register(u2);
RWByteAddressBuffer g_indirectCommandCount : register(u3);
RWByteAddressBuffer g_batchStats : register(u4);

cbuffer BatchParam : register(b0)
{
    uint g_maxBatchCount;
};

cbuffer BucketParam : register(b1)
{
    uint g_maxMaterialCount;
};

cbuffer DepthBinParam : register(b2)
{
    uint g_depthBinCount;
};

cbuffer MaxCommandCountParam : register(b3)
{
    uint g_maxCommandCount;
};

cbuffer IndexStreamModeParam : register(b4)
{
    uint g_useRangeIndexStream;
};

static const uint kStatsCommandCount = 0u;
static const uint kStatsInstanceCount = 4u;
static const uint kStatsSubmittedIndexCount = 8u;
static const uint kStatsOverflowCount = 12u;

void add_saturated_stat(uint byteOffset, uint value)
{
    const uint current = g_batchStats.Load(byteOffset);
    const uint remaining = 0xffffffffu - current;
    g_batchStats.Store(byteOffset, current + min(value, remaining));
}

void emit_command(
    uint batchStart,
    uint instanceCount,
    uint indexCount,
    uint startIndex,
    int baseVertex)
{
    if (indexCount == 0u || instanceCount == 0u)
    {
        return;
    }

    uint commandIndex = 0u;
    g_indirectCommandCount.InterlockedAdd(0u, 1u, commandIndex);
    if (commandIndex >= g_maxCommandCount)
    {
        add_saturated_stat(kStatsOverflowCount, 1u);
        return;
    }

    IndirectCommand indirectCommand;
    indirectCommand.drawObjectStartIndex = batchStart;
    indirectCommand.primitiveBase = 0u;
    indirectCommand.indexCountPerInstance = indexCount;
    indirectCommand.instanceCount = instanceCount;
    indirectCommand.startIndexLocation = startIndex;
    indirectCommand.baseVertexLocation = baseVertex;
    indirectCommand.startInstanceLocation = 0u;
    g_indirectCommands[commandIndex] = indirectCommand;

    add_saturated_stat(kStatsCommandCount, 1u);
    add_saturated_stat(kStatsInstanceCount, instanceCount);
    const uint submittedIndexCount =
        instanceCount != 0u && indexCount > 0xffffffffu / instanceCount
            ? 0xffffffffu
            : indexCount * instanceCount;
    add_saturated_stat(kStatsSubmittedIndexCount, submittedIndexCount);
}

[numthreads(1, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    g_indirectCommandCount.Store(0u, 0u);
    g_batchStats.Store(kStatsCommandCount, 0u);
    g_batchStats.Store(kStatsInstanceCount, 0u);
    g_batchStats.Store(kStatsSubmittedIndexCount, 0u);
    g_batchStats.Store(kStatsOverflowCount, 0u);

    uint runningOffset = 0u;
    for (uint batchId = 0u; batchId < g_maxBatchCount; ++batchId)
    {
        const uint instanceCount = g_batchObjectCounts.Load(batchId * 4u);
        g_batchObjectStarts.Store(batchId * 4u, runningOffset);
        g_batchWriteOffsets.Store(batchId * 4u, runningOffset);
        runningOffset += instanceCount;
    }

    const uint meshCount =
        g_maxBatchCount / max(1u, g_maxMaterialCount * g_depthBinCount);
    for (uint depthBin = 0u; depthBin < g_depthBinCount; ++depthBin)
    {
        for (uint meshId = 0u; meshId < meshCount; ++meshId)
        {
            for (uint materialId = 0u; materialId < g_maxMaterialCount;
                 ++materialId)
            {
                const uint batchId =
                    (meshId * g_maxMaterialCount + materialId) *
                        g_depthBinCount +
                    depthBin;
                if (batchId >= g_maxBatchCount)
                {
                    return;
                }

                const uint instanceCount =
                    g_batchObjectCounts.Load(batchId * 4u);
                if (instanceCount == 0u)
                {
                    continue;
                }

                const MeshRange meshRange = g_meshRanges[meshId];
                const uint indexCount = g_useRangeIndexStream != 0u
                    ? meshRange.rangeIndexCount
                    : meshRange.indexCount;
                const uint startIndex = g_useRangeIndexStream != 0u
                    ? meshRange.rangeStartIndex
                    : meshRange.startIndex;
                if (indexCount == 0u)
                {
                    continue;
                }

                const uint batchStart = g_batchObjectStarts.Load(batchId * 4u);
                emit_command(batchStart, instanceCount, indexCount, startIndex,
                             meshRange.baseVertex);
            }
        }
    }
}
