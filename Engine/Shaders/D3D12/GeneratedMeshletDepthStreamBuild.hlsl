// Build a non-indexed depth vertex stream from the visible meshlet list.

struct MeshRange
{
    uint indexCount;
    uint startIndex;
    int baseVertex;
    uint firstMeshlet;
    uint meshletCount;
    uint rangeStartIndex;
    uint rangeIndexCount;
    uint padding2;
};

struct MeshletBounds
{
    float3 center;
    float radius;
    float3 coneApex;
    float coneCutoff;
    float3 coneAxis;
    uint flags;
    uint firstIndex;
    uint indexCount;
    uint padding0;
    uint padding1;
};

struct VisibleMeshlet
{
    uint objectIndex;
    uint meshId;
    uint meshletIndex;
    uint padding;
};

struct GeneratedTriVertex
{
    uint objectIndex;
    uint sourceVertexIndex;
};

cbuffer MaxGeneratedVertexParam : register(b0)
{
    uint g_maxGeneratedVertexCount;
};

StructuredBuffer<VisibleMeshlet> g_visibleMeshlets : register(t0);
StructuredBuffer<MeshRange> g_meshRanges : register(t2);
StructuredBuffer<MeshletBounds> g_meshletBounds : register(t3);
ByteAddressBuffer g_rangeIndices : register(t4);

RWStructuredBuffer<GeneratedTriVertex> g_generatedVertices : register(u0);
RWByteAddressBuffer g_outCounters : register(u1);

static const uint kVisibleMeshletCounterOffset = 0u;
static const uint kGeneratedVertexCounterOffset = 4u;

bool reserve_vertices(uint vertexCount, out uint baseVertex)
{
    baseVertex = 0u;
    if (vertexCount == 0u)
    {
        return true;
    }

    [loop]
    for (;;)
    {
        const uint currentCount =
            g_outCounters.Load(kGeneratedVertexCounterOffset);
        if (currentCount > g_maxGeneratedVertexCount ||
            vertexCount > g_maxGeneratedVertexCount - currentCount)
        {
            return false;
        }

        uint previousCount = 0u;
        g_outCounters.InterlockedCompareExchange(
            kGeneratedVertexCounterOffset, currentCount,
            currentCount + vertexCount, previousCount);
        if (previousCount == currentCount)
        {
            baseVertex = currentCount;
            return true;
        }
    }
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint visibleIndex = dispatchThreadId.x;
    const uint visibleMeshletCount =
        g_outCounters.Load(kVisibleMeshletCounterOffset);
    if (visibleIndex >= visibleMeshletCount)
    {
        return;
    }

    const VisibleMeshlet visibleMeshlet = g_visibleMeshlets[visibleIndex];
    const MeshRange meshRange = g_meshRanges[visibleMeshlet.meshId];
    const MeshletBounds bounds =
        g_meshletBounds[visibleMeshlet.meshletIndex];
    if (bounds.indexCount == 0u)
    {
        return;
    }

    uint outBase = 0u;
    if (!reserve_vertices(bounds.indexCount, outBase))
    {
        return;
    }

    [loop]
    for (uint indexOffset = 0u; indexOffset < bounds.indexCount;
         ++indexOffset)
    {
        const uint localVertexIndex =
            g_rangeIndices.Load((bounds.firstIndex + indexOffset) * 4u);

        GeneratedTriVertex generated;
        generated.objectIndex = visibleMeshlet.objectIndex;
        generated.sourceVertexIndex =
            (uint)(meshRange.baseVertex + (int)localVertexIndex);
        g_generatedVertices[outBase + indexOffset] = generated;
    }
}
