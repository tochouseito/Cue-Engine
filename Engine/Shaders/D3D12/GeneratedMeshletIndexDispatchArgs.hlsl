ByteAddressBuffer g_meshletCounters : register(t0);
RWByteAddressBuffer g_dispatchArgs : register(u0);

cbuffer GeneratedMeshletIndexDispatchConstants : register(b0)
{
    uint g_maxVisibleMeshletCount;
};

cbuffer GeneratedMeshletCandidateDispatchConstants : register(b1)
{
    uint g_maxCandidateChunkCount;
};

static const uint kVisibleMeshletCounterOffset = 0u;
static const uint kCandidateChunkCounterOffset = 4u;
static const uint kThreadsPerGroup = 64u;

[numthreads(1, 1, 1)]
void CSMain()
{
    const uint visibleCount =
        min(g_meshletCounters.Load(kVisibleMeshletCounterOffset),
            g_maxVisibleMeshletCount);
    const uint candidateCount =
        min(g_meshletCounters.Load(kCandidateChunkCounterOffset),
            g_maxCandidateChunkCount);
    const uint workItemCount = max(visibleCount, candidateCount);
    const uint groupCount = max((workItemCount + kThreadsPerGroup - 1u) /
                                kThreadsPerGroup, 1u);
    g_dispatchArgs.Store3(0u, uint3(groupCount, 1u, 1u));
}
