// Build DispatchIndirect arguments for expanding visible candidate chunks.

struct DispatchArgs
{
    uint threadGroupCountX;
    uint threadGroupCountY;
    uint threadGroupCountZ;
};

cbuffer MaxCandidateChunkParam : register(b0)
{
    uint g_maxCandidateChunkCount;
};

ByteAddressBuffer g_counters : register(t0);
RWStructuredBuffer<DispatchArgs> g_dispatchArgs : register(u0);

static const uint kCandidateChunkCounterOffset = 4u;

[numthreads(1, 1, 1)]
void CSMain()
{
    const uint candidateChunkCount =
        min(g_counters.Load(kCandidateChunkCounterOffset),
            g_maxCandidateChunkCount);

    DispatchArgs args;
    args.threadGroupCountX = (candidateChunkCount + 63u) / 64u;
    args.threadGroupCountY = 1u;
    args.threadGroupCountZ = 1u;
    g_dispatchArgs[0] = args;
}
