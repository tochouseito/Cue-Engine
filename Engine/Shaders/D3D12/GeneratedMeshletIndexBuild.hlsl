#include "GeneratedMeshletIndexCommon.hlsli"

struct VisibleMeshlet
{
    uint objectIndex;
    uint meshId;
    uint meshletIndex;
    uint segmentStartIndex;
};

struct MeshletBounds
{
    float3 center;
    float radius;
    float3 coneApex;
    float coneCutoff;
    float3 coneAxis;
    uint flags;
    uint firstIndex;
    uint indexCount;
    uint padding0;
    uint padding1;
};

cbuffer GeneratedIndexCapacityParam : register(b0)
{
    uint g_maxGeneratedIndexCount;
};

cbuffer VisibleMeshletCapacityParam : register(b1)
{
    uint g_maxVisibleMeshletCount;
};

cbuffer PageStartParam : register(b2)
{
    uint g_pageStart;
};

cbuffer PageSegmentCapacityParam : register(b3)
{
    uint g_pageSegmentCapacity;
};

StructuredBuffer<VisibleMeshlet> g_visibleMeshlets : register(t0);
ByteAddressBuffer g_meshletCounters : register(t1);
StructuredBuffer<MeshletBounds> g_meshletBounds : register(t2);
ByteAddressBuffer g_meshletLocalIndices : register(t3);

RWByteAddressBuffer g_generatedIndices : register(u0);
RWStructuredBuffer<uint> g_outputIndexOffsets : register(u1);
RWByteAddressBuffer g_generatedCounters : register(u2);
RWByteAddressBuffer g_state : register(u3);

static const uint kVisibleMeshletCounterOffset = 0u;
static const uint kGeneratedIndexCounterOffset = 0u;
static const uint kGeneratedOverflowOffset = 4u;
groupshared uint g_groupOutputIndexOffset;
groupshared uint g_groupSegmentIndexCount;

uint load_local_index(uint byteOffset)
{
    const uint alignedOffset = byteOffset & ~3u;
    const uint shift = (byteOffset & 3u) * 8u;
    return (g_meshletLocalIndices.Load(alignedOffset) >> shift) & 0xffu;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 groupId : SV_GroupID,
            uint3 groupThreadId : SV_GroupThreadID)
{
    const uint visibleCount =
        min(g_meshletCounters.Load(kVisibleMeshletCounterOffset),
            g_maxVisibleMeshletCount);
    const uint visibleIndex = g_pageStart + groupId.x;
    if (g_state.Load(kGeneratedStateValidOffset) == 0u ||
        groupId.x >= g_pageSegmentCapacity ||
        visibleIndex >= visibleCount)
    {
        return;
    }

    const VisibleMeshlet visible = g_visibleMeshlets[visibleIndex];
    const MeshletBounds bounds = g_meshletBounds[visible.meshletIndex];
    const uint remainingIndexCount =
        bounds.indexCount > visible.segmentStartIndex
            ? bounds.indexCount - visible.segmentStartIndex
            : 0u;
    const uint segmentIndexCount =
        min(remainingIndexCount, kGeneratedMaxIndicesPerSegment) / 3u * 3u;

    if (groupThreadId.x == 0u)
    {
        uint outputIndexOffset = 0u;
        g_generatedCounters.InterlockedAdd(kGeneratedIndexCounterOffset,
                                           segmentIndexCount,
                                           outputIndexOffset);
        g_groupOutputIndexOffset = outputIndexOffset;
        g_groupSegmentIndexCount = segmentIndexCount;
        g_outputIndexOffsets[visibleIndex] = outputIndexOffset;

        if (segmentIndexCount == 0u ||
            outputIndexOffset >
                g_maxGeneratedIndexCount - segmentIndexCount)
        {
            g_groupSegmentIndexCount = 0u;
            g_generatedCounters.InterlockedOr(kGeneratedOverflowOffset, 1u);
            g_state.InterlockedOr(kGeneratedStateOverflowOffset, 1u);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    const uint localIndexByteOffset =
        bounds.padding0 + visible.segmentStartIndex;
    const uint virtualVertexBase =
        visibleIndex * kGeneratedMaxVerticesPerMeshlet;
    for (uint indexOffset = groupThreadId.x;
         indexOffset < g_groupSegmentIndexCount; indexOffset += 64u)
    {
        const uint localIndex =
            load_local_index(localIndexByteOffset + indexOffset);
        g_generatedIndices.Store(
            (g_groupOutputIndexOffset + indexOffset) * 4u,
            virtualVertexBase + localIndex);
    }
}
