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

cbuffer GeneratedMeshletIndexBuildConstants : register(b0)
{
    uint g_maxGeneratedIndexCount;
};

cbuffer GeneratedMeshletVisibleBuildConstants : register(b1)
{
    uint g_maxVisibleMeshletCount;
};

StructuredBuffer<VisibleMeshlet> g_visibleMeshlets : register(t0);
ByteAddressBuffer g_meshletCounters : register(t1);
StructuredBuffer<MeshletBounds> g_meshletBounds : register(t2);
ByteAddressBuffer g_meshletLocalIndices : register(t3);

RWByteAddressBuffer g_generatedIndices : register(u0);
RWStructuredBuffer<uint> g_outputIndexOffsets : register(u1);
RWByteAddressBuffer g_generatedCounters : register(u2);

static const uint kVisibleMeshletCounterOffset = 0u;
static const uint kGeneratedIndexCounterOffset = 0u;
static const uint kGeneratedOverflowOffset = 4u;
static const uint kMaxOutputIndicesPerSegment = 384u;

uint load_local_index(uint byteOffset)
{
    const uint alignedOffset = byteOffset & ~3u;
    const uint shift = (byteOffset & 3u) * 8u;
    return (g_meshletLocalIndices.Load(alignedOffset) >> shift) & 0xffu;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint visibleCount =
        min(g_meshletCounters.Load(kVisibleMeshletCounterOffset),
            g_maxVisibleMeshletCount);
    const uint visibleIndex = dispatchThreadId.x;
    if (visibleIndex >= visibleCount)
    {
        return;
    }

    // Avoid partially filling the index buffer when the conservative upper
    // bound cannot fit. Finalize observes the overflow flag and keeps the
    // affected objects on the classic visibility path.
    if (visibleCount >
        g_maxGeneratedIndexCount / kMaxOutputIndicesPerSegment)
    {
        if (visibleIndex == 0u)
        {
            g_generatedCounters.InterlockedOr(kGeneratedOverflowOffset, 1u);
        }
        return;
    }

    const VisibleMeshlet visible = g_visibleMeshlets[visibleIndex];
    const MeshletBounds bounds = g_meshletBounds[visible.meshletIndex];
    const uint remainingIndexCount =
        bounds.indexCount > visible.segmentStartIndex
            ? bounds.indexCount - visible.segmentStartIndex
            : 0u;
    const uint segmentIndexCount =
        min(remainingIndexCount, kMaxOutputIndicesPerSegment) / 3u * 3u;

    uint outputIndexOffset = 0u;
    g_generatedCounters.InterlockedAdd(kGeneratedIndexCounterOffset,
                                       segmentIndexCount, outputIndexOffset);
    g_outputIndexOffsets[visibleIndex] = outputIndexOffset;

    if (segmentIndexCount == 0u ||
        outputIndexOffset > g_maxGeneratedIndexCount - segmentIndexCount)
    {
        g_generatedCounters.InterlockedOr(kGeneratedOverflowOffset, 1u);
        return;
    }

    const uint localIndexByteOffset =
        bounds.padding0 + visible.segmentStartIndex;
    const uint virtualVertexBase = visibleIndex * 64u;
    for (uint indexOffset = 0u; indexOffset < segmentIndexCount; ++indexOffset)
    {
        const uint localIndex =
            load_local_index(localIndexByteOffset + indexOffset);
        g_generatedIndices.Store((outputIndexOffset + indexOffset) * 4u,
                                 virtualVertexBase + localIndex);
    }
}
