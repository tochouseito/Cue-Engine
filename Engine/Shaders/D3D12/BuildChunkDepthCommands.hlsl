#include "MeshletChunkVisibility.hlsli"

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

struct MeshChunkRange
{
    uint firstChunk;
    uint chunkCount;
    uint padding0;
    uint padding1;
};

struct MeshletChunk
{
    uint startIndex;
    uint indexCount;
    int baseVertex;
    uint firstMeshlet;
    uint meshletCount;
    uint meshId;
    uint materialId;
    uint lod;
    float3 boundsCenter;
    float boundsRadius;
    float3 coneAxis;
    float coneCutoff;
};

struct IndirectCommand
{
    uint drawObjectStartIndex;
    uint primitiveBase;
    uint indexCountPerInstance;
    uint instanceCount;
    uint startIndexLocation;
    int baseVertexLocation;
    uint startInstanceLocation;
};

cbuffer ObjectCountParam : register(b0)
{
    uint g_objectCount;
};

cbuffer MaxCommandCountParam : register(b1)
{
    uint g_maxCommandCount;
};

cbuffer MaxInstanceCountParam : register(b2)
{
    uint g_maxInstanceCount;
};

cbuffer SeedVisibilityParam : register(b3)
{
    uint g_seedVisibility;
};

cbuffer ViewProjection : register(b4)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
};

cbuffer OccluderMinScreenRadiusPxParam : register(b5)
{
    float g_occluderMinScreenRadiusPx;
};

cbuffer OccluderMaxViewDepthParam : register(b6)
{
    float g_occluderMaxViewDepth;
};

cbuffer ScreenHeightParam : register(b7)
{
    uint g_screenHeight;
};

cbuffer OccluderMaxDepthBinParam : register(b8)
{
    uint g_occluderMaxDepthBin;
};

cbuffer OccluderMinObjectScreenRadiusPxParam : register(b9)
{
    float g_occluderMinObjectScreenRadiusPx;
};

cbuffer MaxChunksPerObjectParam : register(b10)
{
    uint g_maxChunksPerObject;
};

StructuredBuffer<RenderObject> g_renderObjects : register(t0);
StructuredBuffer<Transform> g_transforms : register(t1);
ByteAddressBuffer g_renderObjectCount : register(t2);
StructuredBuffer<MeshChunkRange> g_meshChunkRanges : register(t3);
StructuredBuffer<MeshletChunk> g_meshletChunks : register(t4);

RWStructuredBuffer<IndirectCommand> g_commands : register(u0);
RWByteAddressBuffer g_counters : register(u1);
RWStructuredBuffer<uint> g_instanceList : register(u2);
RWByteAddressBuffer g_currentVisibilityBits : register(u3);
RWByteAddressBuffer g_previousVisibilityBits : register(u4);
RWByteAddressBuffer g_stats : register(u5);

static const uint kCounterCommandCount = 0u;
static const uint kCounterInstanceCount = 4u;

static const uint kStatsTestedObjectChunkCount = 16u;
static const uint kStatsRejectedByPreviousVisibility = 20u;
static const uint kStatsRejectedByFrustum = 24u;
static const uint kStatsCommandOverflowCount = 28u;
static const uint kStatsTotalObjectCount = 32u;
static const uint kStatsVisibleObjectCount = 36u;
static const uint kStatsCulledObjectCount = 40u;
static const uint kStatsRejectedByOccluderFilter = 44u;
static const uint kStatsRejectedByObjectFilter = 48u;
static const uint kStatsSkippedChunksByObjectFilter = 52u;
static const uint kStatsSkippedChunksByMaxChunks = 56u;
static const uint kStatsSelectedObjectCount = 60u;
static const uint kStatsMaxChunksPerObject = 64u;
static const uint kStatsMinObjectScreenRadiusPx = 68u;
static const uint kStatsMinChunkScreenRadiusPx = 72u;
static const uint kStatsMaxViewDepth = 76u;
static const uint kStatsMaxDepthBin = 80u;

float transform_radius_scale(float4x4 worldMatrix)
{
    return max(length(worldMatrix[0].xyz),
               max(length(worldMatrix[1].xyz), length(worldMatrix[2].xyz)));
}

bool is_sphere_inside_plane(float4 plane, float3 center, float radius)
{
    const float invPlaneLength =
        rsqrt(max(dot(plane.xyz, plane.xyz), 0.000000000001f));
    const float signedDistance =
        (dot(plane.xyz, center) + plane.w) * invPlaneLength;
    return signedDistance >= -radius;
}

bool is_view_sphere_inside_frustum(float3 viewCenter, float radius)
{
    const float4 projectionColumn0 =
        float4(g_projectionMatrix[0][0], g_projectionMatrix[1][0],
               g_projectionMatrix[2][0], g_projectionMatrix[3][0]);
    const float4 projectionColumn1 =
        float4(g_projectionMatrix[0][1], g_projectionMatrix[1][1],
               g_projectionMatrix[2][1], g_projectionMatrix[3][1]);
    const float4 projectionColumn2 =
        float4(g_projectionMatrix[0][2], g_projectionMatrix[1][2],
               g_projectionMatrix[2][2], g_projectionMatrix[3][2]);
    const float4 projectionColumn3 =
        float4(g_projectionMatrix[0][3], g_projectionMatrix[1][3],
               g_projectionMatrix[2][3], g_projectionMatrix[3][3]);

    if (!is_sphere_inside_plane(
            projectionColumn3 + projectionColumn0, viewCenter, radius))
        return false;
    if (!is_sphere_inside_plane(
            projectionColumn3 - projectionColumn0, viewCenter, radius))
        return false;
    if (!is_sphere_inside_plane(
            projectionColumn3 + projectionColumn1, viewCenter, radius))
        return false;
    if (!is_sphere_inside_plane(
            projectionColumn3 - projectionColumn1, viewCenter, radius))
        return false;
    if (!is_sphere_inside_plane(
            projectionColumn3 + projectionColumn2, viewCenter, radius))
        return false;
    if (!is_sphere_inside_plane(
            projectionColumn3 - projectionColumn2, viewCenter, radius))
        return false;
    return true;
}

bool reserve_command(out uint commandIndex)
{
    commandIndex = 0u;

    uint previousCommandCount = 0u;
    g_counters.InterlockedAdd(kCounterCommandCount, 1u,
                              previousCommandCount);
    if (previousCommandCount >= g_maxCommandCount)
    {
        g_stats.InterlockedAdd(kStatsCommandOverflowCount, 1u);
        return false;
    }

    uint previousInstanceCount = 0u;
    g_counters.InterlockedAdd(kCounterInstanceCount, 1u,
                              previousInstanceCount);
    if (previousInstanceCount >= g_maxInstanceCount)
    {
        g_stats.InterlockedAdd(kStatsCommandOverflowCount, 1u);
        return false;
    }

    commandIndex = previousCommandCount;
    return true;
}

bool passes_occluder_filter(float viewDepth, float viewRadius)
{
    if (viewDepth <= 0.001f)
    {
        return false;
    }

    if (g_occluderMaxViewDepth > 0.0f && viewDepth > g_occluderMaxViewDepth)
    {
        return false;
    }

    if (g_occluderMinScreenRadiusPx > 0.0f && g_screenHeight > 0u)
    {
        const float projectedRadiusPx =
            viewRadius * g_projectionMatrix[1][1] *
            ((float)g_screenHeight * 0.5f) / max(viewDepth, 0.001f);
        if (projectedRadiusPx < g_occluderMinScreenRadiusPx)
        {
            return false;
        }
    }

    return true;
}

bool passes_object_filter(float4 boundsCenterRadius)
{
    const float4 viewCenter =
        mul(float4(boundsCenterRadius.xyz, 1.0f), g_viewMatrix);
    const float viewDepth = viewCenter.z;
    if (viewDepth <= 0.001f)
    {
        return false;
    }

    if (g_occluderMaxViewDepth > 0.0f && viewDepth > g_occluderMaxViewDepth)
    {
        return false;
    }

    if (g_occluderMinObjectScreenRadiusPx > 0.0f && g_screenHeight > 0u)
    {
        const float projectedRadiusPx =
            boundsCenterRadius.w * g_projectionMatrix[1][1] *
            ((float)g_screenHeight * 0.5f) / max(viewDepth, 0.001f);
        if (projectedRadiusPx < g_occluderMinObjectScreenRadiusPx)
        {
            return false;
        }
    }

    return true;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint objectIndex = dispatchThreadId.x;
    const uint visibleObjectCount = g_renderObjectCount.Load(0);
    if (objectIndex == 0u)
    {
        g_stats.Store(kStatsTotalObjectCount, g_objectCount);
        g_stats.Store(kStatsVisibleObjectCount, visibleObjectCount);
        g_stats.Store(kStatsCulledObjectCount,
                      g_objectCount > visibleObjectCount
                          ? g_objectCount - visibleObjectCount
                          : 0u);
        g_stats.Store(kStatsMaxChunksPerObject, g_maxChunksPerObject);
        g_stats.Store(kStatsMinObjectScreenRadiusPx,
                      (uint)g_occluderMinObjectScreenRadiusPx);
        g_stats.Store(kStatsMinChunkScreenRadiusPx,
                      (uint)g_occluderMinScreenRadiusPx);
        g_stats.Store(kStatsMaxViewDepth, (uint)g_occluderMaxViewDepth);
        g_stats.Store(kStatsMaxDepthBin, g_occluderMaxDepthBin);
    }

    if (objectIndex >= visibleObjectCount || objectIndex >= g_objectCount)
    {
        return;
    }

    const RenderObject renderObject = g_renderObjects[objectIndex];
    if ((renderObject.drawFlags & 1u) != 0u)
    {
        return;
    }

    if (renderObject.depthBin > g_occluderMaxDepthBin)
    {
        return;
    }

    const MeshChunkRange chunkRange = g_meshChunkRanges[renderObject.meshId];
    if (chunkRange.chunkCount == 0u)
    {
        return;
    }

    if (!passes_object_filter(renderObject.boundsCenterRadius))
    {
        g_stats.InterlockedAdd(kStatsRejectedByObjectFilter, 1u);
        g_stats.InterlockedAdd(kStatsSkippedChunksByObjectFilter,
                               chunkRange.chunkCount);
        return;
    }
    g_stats.InterlockedAdd(kStatsSelectedObjectCount, 1u);

    const Transform transform = g_transforms[renderObject.transformId];
    const float radiusScale = transform_radius_scale(transform.worldMatrix);
    const uint selectedChunkCount =
        g_maxChunksPerObject == 0u
            ? chunkRange.chunkCount
            : min(chunkRange.chunkCount, g_maxChunksPerObject);
    if (selectedChunkCount < chunkRange.chunkCount)
    {
        g_stats.InterlockedAdd(kStatsSkippedChunksByMaxChunks,
                               chunkRange.chunkCount - selectedChunkCount);
    }

    [loop]
    for (uint selectedChunkIndex = 0u; selectedChunkIndex < selectedChunkCount;
         ++selectedChunkIndex)
    {
        const uint localChunkIndex =
            selectedChunkCount == chunkRange.chunkCount
                ? selectedChunkIndex
                : min((selectedChunkIndex * chunkRange.chunkCount) /
                          selectedChunkCount,
                      chunkRange.chunkCount - 1u);
        const uint globalChunkId = chunkRange.firstChunk + localChunkIndex;
        const MeshletChunk chunk = g_meshletChunks[globalChunkId];
        if (chunk.indexCount == 0u)
        {
            continue;
        }

        g_stats.InterlockedAdd(kStatsTestedObjectChunkCount, 1u);

        const bool usePreviousVisibilityFilter = false;
        const bool wasVisible =
            !usePreviousVisibilityFilter || g_seedVisibility != 0u ||
            WasChunkVisibleLastFrame(g_previousVisibilityBits, globalChunkId);
        if (!wasVisible)
        {
            g_stats.InterlockedAdd(kStatsRejectedByPreviousVisibility, 1u);
            continue;
        }

        const float4 worldCenter =
            mul(float4(chunk.boundsCenter, 1.0f), transform.worldMatrix);
        const float4 viewCenter = mul(worldCenter, g_viewMatrix);
        const float viewRadius = chunk.boundsRadius * radiusScale;
        if (!is_view_sphere_inside_frustum(viewCenter.xyz, viewRadius))
        {
            g_stats.InterlockedAdd(kStatsRejectedByFrustum, 1u);
            continue;
        }

        if (!passes_occluder_filter(viewCenter.z, viewRadius))
        {
            g_stats.InterlockedAdd(kStatsRejectedByOccluderFilter, 1u);
            continue;
        }

        MarkChunkVisibleThisFrame(g_currentVisibilityBits, globalChunkId);

        uint commandIndex = 0u;
        if (!reserve_command(commandIndex))
        {
            continue;
        }

        g_instanceList[commandIndex] = objectIndex;

        IndirectCommand command;
        command.drawObjectStartIndex = commandIndex;
        command.primitiveBase = 0u;
        command.indexCountPerInstance = chunk.indexCount;
        command.instanceCount = 1u;
        command.startIndexLocation = chunk.startIndex;
        command.baseVertexLocation = chunk.baseVertex;
        command.startInstanceLocation = 0u;
        g_commands[commandIndex] = command;
    }
}
