// Build DispatchIndirect arguments for mesh shader visibility meshlet culling.

struct DispatchArgs
{
    uint threadGroupCountX;
    uint threadGroupCountY;
    uint threadGroupCountZ;
};

ByteAddressBuffer g_visibleObjectCount : register(t0);
RWStructuredBuffer<DispatchArgs> g_dispatchArgs : register(u0);

cbuffer MaxObjectParam : register(b0)
{
    uint g_maxObjectCount;
};

[numthreads(1, 1, 1)]
void CSMain()
{
    const uint visibleObjectCount =
        min(g_visibleObjectCount.Load(0), g_maxObjectCount);

    DispatchArgs args;
    args.threadGroupCountX = (visibleObjectCount + 63u) / 64u;
    args.threadGroupCountY = 1u;
    args.threadGroupCountZ = 1u;
    g_dispatchArgs[0] = args;
}
