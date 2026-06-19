// Static mesh batching の最終段。
// BatchCount/PrefixSum/BatchFill が作った batch ごとの instance 範囲を、
// ExecuteIndirect が読める DrawIndexedInstanced コマンド列へ変換する。

struct MeshRange
{
    uint indexCount;
    uint startIndex;
    int baseVertex;
    uint firstMeshlet;
    uint meshletCount;
    uint padding0;
    uint padding1;
    uint padding2;
};

struct MeshletBounds
{
    float3 center;
    float radius;
    uint firstIndex;
    uint indexCount;
    uint padding0;
    uint padding1;
};

struct RenderObject
{
    uint objectId;
    uint meshId;
    uint transformId;
    uint materialId;
    uint castsShadow;
    uint receivesShadow;
    uint shadowCasterMode;
    uint skinPaletteOffset;
    uint skinPaletteCount;
    uint drawFlags;
    uint depthBin;
    uint padding;
    float4 boundsCenterRadius;
};

struct Transform
{
    row_major float4x4 worldMatrix;
    row_major float4x4 normalMatrix;
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
StructuredBuffer<uint> g_renderObjectIndices : register(t3);
StructuredBuffer<RenderObject> g_renderObjects : register(t4);
StructuredBuffer<Transform> g_transforms : register(t5);
StructuredBuffer<MeshletBounds> g_meshletBounds : register(t6);

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

cbuffer MaxCommandCountParam : register(b3)
{
    uint g_maxCommandCount;
};

cbuffer MeshletRangeParam : register(b4)
{
    uint g_useMeshletRanges;
};

cbuffer MeshletDepthBinParam : register(b5)
{
    uint g_meshletDepthBinCount;
};

cbuffer ViewProjection : register(b6)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
};

float project_device_depth(float viewZ)
{
    const float4 clipPosition =
        mul(float4(0.0f, 0.0f, viewZ, 1.0f), g_projectionMatrix);
    if (abs(clipPosition.w) <= 0.000001f)
    {
        return 1.0f;
    }
    return saturate(clipPosition.z / clipPosition.w);
}

uint project_depth_bin(float3 worldCenter, uint binCount)
{
    const float4 viewPosition = mul(float4(worldCenter, 1.0f), g_viewMatrix);
    const float deviceDepth = project_device_depth(max(viewPosition.z, 0.001f));
    return min((uint)floor(deviceDepth * (float)binCount), binCount - 1u);
}

void emit_command(
    uint batchStart,
    uint instanceCount,
    uint indexCount,
    uint startIndex,
    int baseVertex)
{
    if (indexCount == 0u || instanceCount == 0u)
    {
        return;
    }

    uint commandIndex = 0u;
    g_indirectCommandCount.InterlockedAdd(0, 1u, commandIndex);
    if (commandIndex >= g_maxCommandCount)
    {
        return;
    }

    IndirectCommand indirectCommand;
    indirectCommand.drawObjectStartIndex = batchStart;
    indirectCommand.indexCountPerInstance = indexCount;
    indirectCommand.instanceCount = instanceCount;
    indirectCommand.startIndexLocation = startIndex;
    indirectCommand.baseVertexLocation = baseVertex;
    indirectCommand.startInstanceLocation = 0u;
    g_indirectCommands[commandIndex] = indirectCommand;
}

[numthreads(1, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    // Emit commands near-to-far so the occluder depth pass benefits from
    // early depth rejection. depthBin 0 is nearest.
    const uint meshletDepthBinCount = max(g_meshletDepthBinCount, 1u);
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

                const uint batchStart = g_batchObjectStarts.Load(batchId * 4u);
                const bool useMeshletRanges =
                    g_useMeshletRanges != 0u &&
                    meshRange.meshletCount > 1u;

                if (!useMeshletRanges)
                {
                    emit_command(
                        batchStart,
                        instanceCount,
                        meshRange.indexCount,
                        meshRange.startIndex,
                        meshRange.baseVertex);
                    continue;
                }

                const uint representativeRenderObjectIndex =
                    g_renderObjectIndices[batchStart];
                const RenderObject representativeObject =
                    g_renderObjects[representativeRenderObjectIndex];
                const Transform representativeTransform =
                    g_transforms[representativeObject.transformId];

                for (uint meshletDepthBin = 0u;
                     meshletDepthBin < meshletDepthBinCount;
                     ++meshletDepthBin)
                {
                    for (uint meshletOffset = 0u;
                         meshletOffset < meshRange.meshletCount;
                         ++meshletOffset)
                    {
                        const MeshletBounds meshlet =
                            g_meshletBounds[meshRange.firstMeshlet + meshletOffset];
                        if (meshlet.indexCount == 0u)
                        {
                            continue;
                        }

                        const float3 worldCenter =
                            mul(float4(meshlet.center, 1.0f),
                                representativeTransform.worldMatrix).xyz;
                        if (project_depth_bin(worldCenter, meshletDepthBinCount) !=
                            meshletDepthBin)
                        {
                            continue;
                        }

                        emit_command(
                            batchStart,
                            instanceCount,
                            meshlet.indexCount,
                            meshRange.startIndex + meshlet.firstIndex,
                            meshRange.baseVertex);
                    }
                }
            }
        }
    }
}
