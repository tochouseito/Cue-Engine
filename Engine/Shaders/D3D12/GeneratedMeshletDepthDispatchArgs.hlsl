// Build DispatchIndirect arguments for visible meshlet stream generation.

struct DispatchArgs
{
    uint threadGroupCountX;
    uint threadGroupCountY;
    uint threadGroupCountZ;
};

ByteAddressBuffer g_counters : register(t0);
RWStructuredBuffer<DispatchArgs> g_dispatchArgs : register(u0);

static const uint kVisibleMeshletCounterOffset = 0u;

[numthreads(1, 1, 1)]
void CSMain()
{
    const uint visibleMeshletCount =
        g_counters.Load(kVisibleMeshletCounterOffset);

    DispatchArgs args;
    args.threadGroupCountX = (visibleMeshletCount + 63u) / 64u;
    args.threadGroupCountY = 1u;
    args.threadGroupCountZ = 1u;
    g_dispatchArgs[0] = args;
}
