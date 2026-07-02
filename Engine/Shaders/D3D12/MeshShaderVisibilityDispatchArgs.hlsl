// Build DispatchMeshIndirect arguments from the compact visible segment counter.

struct DispatchArgs
{
    uint threadGroupCountX;
    uint threadGroupCountY;
    uint threadGroupCountZ;
};

ByteAddressBuffer g_counters : register(t0);
RWStructuredBuffer<DispatchArgs> g_dispatchArgs : register(u0);

static const uint kCandidateChunkCounterOffset = 4u;
static const uint kVisibleMeshletCounterOffset = 0u;

cbuffer MaxCandidateChunkParam : register(b0)
{
    uint g_maxCandidateChunkCount;
};

cbuffer MeshletsPerDispatchGroupParam : register(b1)
{
    uint g_meshletsPerDispatchGroup;
};

[numthreads(1, 1, 1)]
void CSMain()
{
    const uint visibleMeshletCount =
        min(g_counters.Load(kVisibleMeshletCounterOffset),
            g_maxCandidateChunkCount);
    const uint meshletsPerGroup = max(g_meshletsPerDispatchGroup, 1u);

    DispatchArgs args;
    args.threadGroupCountX =
        (visibleMeshletCount + meshletsPerGroup - 1u) / meshletsPerGroup;
    args.threadGroupCountY = 1u;
    args.threadGroupCountZ = 1u;
    g_dispatchArgs[0] = args;
}
