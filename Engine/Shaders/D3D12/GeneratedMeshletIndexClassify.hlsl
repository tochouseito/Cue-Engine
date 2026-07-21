#include "GeneratedMeshletIndexCommon.hlsli"

struct CandidateChunk
{
    uint objectIndex;
    uint meshId;
    uint firstMeshlet;
    uint meshletCountAndSegmentCount;
};

cbuffer MaxVisibleMeshletParam : register(b0)
{
    uint g_maxVisibleMeshletCount;
};

cbuffer MaxCandidateChunkParam : register(b1)
{
    uint g_maxCandidateChunkCount;
};

cbuffer SegmentsPerPageParam : register(b2)
{
    uint g_segmentsPerPage;
};

cbuffer PageCountParam : register(b3)
{
    uint g_pageCount;
};

ByteAddressBuffer g_meshletCounters : register(t0);
StructuredBuffer<CandidateChunk> g_candidateChunks : register(t1);

RWByteAddressBuffer g_objectDrawModes : register(u0);
RWByteAddressBuffer g_pageDispatchArgs : register(u1);
RWByteAddressBuffer g_state : register(u2);

static const uint kVisibleMeshletCounterOffset = 0u;
static const uint kCandidateChunkCounterOffset = 4u;
static const uint kGeneratedDrawMode = 4u;

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint visibleCount =
        g_meshletCounters.Load(kVisibleMeshletCounterOffset);
    const uint candidateCount =
        g_meshletCounters.Load(kCandidateChunkCounterOffset);
    const uint totalSegmentCapacity = g_segmentsPerPage * g_pageCount;
    const bool valid =
        visibleCount != 0u &&
        visibleCount <= g_maxVisibleMeshletCount &&
        visibleCount <= totalSegmentCapacity &&
        candidateCount <= g_maxCandidateChunkCount;

    if (dispatchThreadId.x == 0u)
    {
        const uint activePageCount =
            valid
                ? (visibleCount + g_segmentsPerPage - 1u) /
                      g_segmentsPerPage
                : 0u;
        g_state.Store(kGeneratedStateValidOffset, valid ? 1u : 0u);
        g_state.Store(kGeneratedStateVisibleCountOffset, visibleCount);
        g_state.Store(kGeneratedStateCandidateCountOffset, candidateCount);
        g_state.Store(kGeneratedStateActivePageCountOffset, activePageCount);
        g_state.Store(kGeneratedStateIndexCountOffset, 0u);
        g_state.Store(kGeneratedStateDrawCountOffset, 0u);
        g_state.Store(kGeneratedStateOverflowOffset, 0u);
        g_state.Store(kGeneratedStateFallbackOffset,
                      !valid && visibleCount != 0u ? 1u : 0u);

        [loop]
        for (uint pageIndex = 0u; pageIndex < g_pageCount; ++pageIndex)
        {
            const uint pageStart = pageIndex * g_segmentsPerPage;
            const uint pageVisibleCount =
                valid && visibleCount > pageStart
                    ? min(visibleCount - pageStart, g_segmentsPerPage)
                    : 0u;
            const uint byteOffset = pageIndex * 12u;
            g_pageDispatchArgs.Store3(
                byteOffset, uint3(max(pageVisibleCount, 1u), 1u, 1u));
        }
    }

    const uint candidateIndex = dispatchThreadId.x;
    if (valid && candidateIndex < candidateCount)
    {
        const uint objectIndex = g_candidateChunks[candidateIndex].objectIndex;
        g_objectDrawModes.Store(objectIndex * 4u, kGeneratedDrawMode);
    }
}
