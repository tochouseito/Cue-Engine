struct VisibleMeshlet
{
    uint objectIndex;
    uint meshId;
    uint meshletIndex;
    uint segmentStartIndex;
};

struct CandidateChunk
{
    uint objectIndex;
    uint meshId;
    uint firstMeshlet;
    uint meshletCountAndSegmentCount;
};

cbuffer GeneratedMeshletIndexFinalizeConstants : register(b0)
{
    uint g_maxVisibleMeshletCount;
};

cbuffer GeneratedMeshletCandidateFinalizeConstants : register(b1)
{
    uint g_maxCandidateChunkCount;
};

cbuffer GeneratedMeshletCapacityFinalizeConstants : register(b2)
{
    uint g_maxGeneratedIndexCount;
};

StructuredBuffer<VisibleMeshlet> g_visibleMeshlets : register(t0);
ByteAddressBuffer g_meshletCounters : register(t1);
ByteAddressBuffer g_generatedCounters : register(t2);
StructuredBuffer<CandidateChunk> g_candidateChunks : register(t3);

RWByteAddressBuffer g_objectDrawModes : register(u0);
RWByteAddressBuffer g_indirectCommand : register(u1);
RWByteAddressBuffer g_indirectCommandCount : register(u2);

static const uint kVisibleMeshletCounterOffset = 0u;
static const uint kCandidateChunkCounterOffset = 4u;
static const uint kGeneratedIndexCounterOffset = 0u;
static const uint kGeneratedOverflowOffset = 4u;
static const uint kGeneratedDrawMode = 4u;

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint visibleCount =
        g_meshletCounters.Load(kVisibleMeshletCounterOffset);
    const uint candidateCount =
        g_meshletCounters.Load(kCandidateChunkCounterOffset);
    const uint generatedIndexCount =
        g_generatedCounters.Load(kGeneratedIndexCounterOffset);
    const uint generatedOverflow =
        g_generatedCounters.Load(kGeneratedOverflowOffset);
    const bool valid =
        visibleCount <= g_maxVisibleMeshletCount &&
        candidateCount <= g_maxCandidateChunkCount &&
        generatedIndexCount <= g_maxGeneratedIndexCount &&
        (generatedIndexCount % 3u) == 0u && generatedOverflow == 0u;

    if (dispatchThreadId.x == 0u)
    {
        if (valid && visibleCount != 0u && generatedIndexCount != 0u)
        {
            g_indirectCommand.Store(0u, 0u);
            g_indirectCommand.Store(4u, 0u);
            g_indirectCommand.Store(8u, generatedIndexCount);
            g_indirectCommand.Store(12u, 1u);
            g_indirectCommand.Store(16u, 0u);
            g_indirectCommand.Store(20u, 0u);
            g_indirectCommand.Store(24u, 0u);
            g_indirectCommandCount.Store(0u, 1u);
        }
        else
        {
            g_indirectCommandCount.Store(0u, 0u);
        }
    }

    const uint candidateIndex = dispatchThreadId.x;
    if (valid && candidateIndex < candidateCount)
    {
        const uint objectIndex = g_candidateChunks[candidateIndex].objectIndex;
        g_objectDrawModes.Store(objectIndex * 4u, kGeneratedDrawMode);
    }
}
