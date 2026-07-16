// Builds prefix sums from the per-batch instance counts produced by BatchCountPass.
// The resulting start/writeOffset values define the DrawInstanceBuffer ranges
// filled by BatchFillPass.

ByteAddressBuffer g_batchObjectCounts : register(t0);
RWByteAddressBuffer g_batchObjectStarts : register(u0);
RWByteAddressBuffer g_batchWriteOffsets : register(u1);

cbuffer BatchParam : register(b0)
{
    uint g_maxBatchCount;
};

[numthreads(1, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    // The current batch count is small enough for a single-thread prefix sum.
    // A larger mesh/material/PSO space can replace this with a parallel scan.
    uint runningOffset = 0u;
    for (uint batchId = 0u; batchId < g_maxBatchCount; ++batchId)
    {
        const uint instanceCount = g_batchObjectCounts.Load(batchId * 4u);
        g_batchObjectStarts.Store(batchId * 4u, runningOffset);
        g_batchWriteOffsets.Store(batchId * 4u, runningOffset);
        runningOffset += instanceCount;
    }
}
