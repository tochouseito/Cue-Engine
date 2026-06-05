// Static mesh batching の最終段。
// BatchCount/PrefixSum/BatchFill が作った batch ごとの instance 範囲を、
// ExecuteIndirect が読める DrawIndexedInstanced コマンド列へ変換する。

struct MeshRange
{
    uint indexCount;
    uint startIndex;
    int baseVertex;
    uint padding;
};

struct IndirectCommand
{
    uint drawObjectStartIndex;
    uint indexCountPerInstance;
    uint instanceCount;
    uint startIndexLocation;
    int baseVertexLocation;
    uint startInstanceLocation;
};

StructuredBuffer<MeshRange> g_meshRanges : register(t0);
ByteAddressBuffer g_batchObjectCounts : register(t1);
ByteAddressBuffer g_batchObjectStarts : register(t2);

RWStructuredBuffer<IndirectCommand> g_indirectCommands : register(u0);
RWByteAddressBuffer g_indirectCommandCount : register(u1);

cbuffer BatchParam : register(b0)
{
    uint g_maxBatchCount;
};

cbuffer BucketParam : register(b1)
{
    uint g_maxMaterialCount;
};

cbuffer DepthBinParam : register(b2)
{
    uint g_depthBinCount;
};

[numthreads(1, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    // Emit commands near-to-far so the occluder depth pass benefits from
    // early depth rejection. depthBin 0 is nearest.
    for (uint depthBin = 0u; depthBin < g_depthBinCount; ++depthBin)
    {
        for (uint meshId = 0u; meshId < g_maxBatchCount / (g_maxMaterialCount * g_depthBinCount); ++meshId)
        {
            for (uint materialId = 0u; materialId < g_maxMaterialCount; ++materialId)
            {
                const uint batchId =
                    (meshId * g_maxMaterialCount + materialId) *
                        g_depthBinCount +
                    depthBin;
                if (batchId >= g_maxBatchCount)
                {
                    return;
                }

                // batch が空なら command を作らない。ここで draw count を
                // compact するため、CPU 側は最大 batch 数を指定するだけでよい。
                const uint instanceCount =
                    g_batchObjectCounts.Load(batchId * 4u);
                if (instanceCount == 0u)
                {
                    continue;
                }

                // batch key の meshId に対応する LOD mesh range を使い、
                // 同じ mesh/material/depthBin の object を 1 draw にまとめる。
                const MeshRange meshRange = g_meshRanges[meshId];
                if (meshRange.indexCount == 0u)
                {
                    continue;
                }

                uint commandIndex = 0;
                g_indirectCommandCount.InterlockedAdd(0, 1u, commandIndex);

                IndirectCommand indirectCommand;
                indirectCommand.drawObjectStartIndex =
                    g_batchObjectStarts.Load(batchId * 4u);
                indirectCommand.indexCountPerInstance = meshRange.indexCount;
                indirectCommand.instanceCount = instanceCount;
                indirectCommand.startIndexLocation = meshRange.startIndex;
                indirectCommand.baseVertexLocation = meshRange.baseVertex;
                indirectCommand.startInstanceLocation = 0u;
                g_indirectCommands[commandIndex] = indirectCommand;
            }
        }
    }
}
