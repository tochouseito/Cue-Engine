// BatchCountPass の batch ごとの instance count から prefix sum を作る。
// 出力された start/writeOffset は BatchFillPass が DrawInstanceBuffer へ
// object index を詰めるための書き込み範囲になる。

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
    // batch 数は現状小さいため single thread で直列 prefix sum する。
    // 将来 mesh/material/PSO が増えて batch 数が増えたら parallel scan に置き換える。
    uint runningOffset = 0u;
    for (uint batchId = 0u; batchId < g_maxBatchCount; ++batchId)
    {
        const uint instanceCount = g_batchObjectCounts.Load(batchId * 4u);
        g_batchObjectStarts.Store(batchId * 4u, runningOffset);
        g_batchWriteOffsets.Store(batchId * 4u, runningOffset);
        runningOffset += instanceCount;
    }
}
