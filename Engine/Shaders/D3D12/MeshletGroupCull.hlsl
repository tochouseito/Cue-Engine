// Meshlet group culling pass.
// Visible objects with useful partial visibility are converted into compact
// DrawIndexedInstanced indirect range commands. Other objects stay on the
// ordinary static mesh batching path.

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
    uint indexCountPerInstance;
    uint instanceCount;
    uint startIndexLocation;
    int baseVertexLocation;
    uint startInstanceLocation;
};

cbuffer DispatchParam : register(b0)
{
    uint g_objectCount;
};

cbuffer MaxCommandParam : register(b1)
{
    uint g_maxRangeCommandCount;
};

cbuffer ViewProjection : register(b2)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
};

StructuredBuffer<RenderObject> g_renderObjects : register(t0);
StructuredBuffer<Transform> g_transforms : register(t1);
ByteAddressBuffer g_renderObjectCount : register(t2);
StructuredBuffer<MeshRange> g_meshRanges : register(t3);
StructuredBuffer<MeshletBounds> g_meshletBounds : register(t4);
StructuredBuffer<MeshChunkRange> g_meshChunkRanges : register(t5);
StructuredBuffer<MeshletChunk> g_meshletChunks : register(t6);

RWStructuredBuffer<uint> g_objectDrawModes : register(u0);
RWStructuredBuffer<IndirectCommand> g_rangeCommands : register(u1);
RWByteAddressBuffer g_rangeCommandCount : register(u2);
RWByteAddressBuffer g_stats : register(u3);

static const uint kDrawModeNormal = 0u;
static const uint kDrawModeGroupRange = 1u;
static const uint kDrawModeCulled = 2u;
static const uint kDrawModeFallback = 3u;
static const uint kMeshletsPerGroup = 8u;
static const uint kMaxRangesPerObject = 64u;
static const uint kMaxMergeGapIndices = 48u;
static const uint kMaxChunkGroupsPerObjectForRange = 128u;
static const bool kEnableMeshletRangeDraws = true;
static const bool kEnableConeCulling = false;
static const uint kRenderObjectFlagForwardFallback = 1u << 0u;
static const float kMinMeshletRangeProjectedRadius = 0.25f;
static const float kMeshletRangeFrustumRadiusScale = 1.35f;
static const uint kMaxRangeDrawnPercent = 92u;
static const float kConeCutoffEpsilon = 0.02f;

static const uint kStatsVisibleObjectCount = 0u;
static const uint kStatsCandidateObjectCount = 4u;
static const uint kStatsTestedGroupCount = 8u;
static const uint kStatsFrustumRejectedGroupCount = 12u;
static const uint kStatsConeTestedMeshletCount = 16u;
static const uint kStatsConeRejectedMeshletCount = 20u;
static const uint kStatsVisibleGroupCount = 24u;
static const uint kStatsCulledObjectCount = 28u;
static const uint kStatsFallbackObjectCount = 32u;
static const uint kStatsRangeObjectCount = 36u;
static const uint kStatsRangeCommandCount = 40u;
static const uint kStatsRangeIndexCount = 44u;
static const uint kStatsTotalIndexCount = 48u;
static const uint kStatsSettings = 52u;

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
    {
        return false;
    }
    if (!is_sphere_inside_plane(
            projectionColumn3 - projectionColumn0, viewCenter, radius))
    {
        return false;
    }
    if (!is_sphere_inside_plane(
            projectionColumn3 + projectionColumn1, viewCenter, radius))
    {
        return false;
    }
    if (!is_sphere_inside_plane(
            projectionColumn3 - projectionColumn1, viewCenter, radius))
    {
        return false;
    }
    if (!is_sphere_inside_plane(
            projectionColumn3 + projectionColumn2, viewCenter, radius))
    {
        return false;
    }
    if (!is_sphere_inside_plane(
            projectionColumn3 - projectionColumn2, viewCenter, radius))
    {
        return false;
    }
    return true;
}

float transform_radius_scale(float4x4 worldMatrix)
{
    return max(length(worldMatrix[0].xyz),
               max(length(worldMatrix[1].xyz), length(worldMatrix[2].xyz)));
}

float projected_radius(float4 boundsCenterRadius)
{
    const float4 viewCenter =
        mul(float4(boundsCenterRadius.xyz, 1.0f), g_viewMatrix);
    const float viewZ = max(viewCenter.z, 0.001f);
    return boundsCenterRadius.w * abs(g_projectionMatrix[1][1]) / viewZ;
}

bool is_meshlet_backfacing(
    MeshletBounds bounds,
    Transform transform,
    float radiusScale)
{
    if (!kEnableConeCulling || (bounds.flags & 1u) == 0u ||
        bounds.coneCutoff <= 0.0f)
    {
        return false;
    }

    const float4 worldApex =
        mul(float4(bounds.coneApex, 1.0f), transform.worldMatrix);
    const float3 viewApex = mul(worldApex, g_viewMatrix).xyz;
    const float distanceSq = dot(viewApex, viewApex);
    const float worldRadius = bounds.radius * radiusScale;
    if (distanceSq <= max(worldRadius * worldRadius, 0.000001f))
    {
        return false;
    }

    const float3 worldAxis =
        mul(float4(bounds.coneAxis, 0.0f), transform.normalMatrix).xyz;
    const float worldAxisLenSq = dot(worldAxis, worldAxis);
    if (worldAxisLenSq <= 0.000001f)
    {
        return false;
    }

    const float3 viewAxis =
        mul(float4(worldAxis * rsqrt(worldAxisLenSq), 0.0f), g_viewMatrix).xyz;
    const float viewAxisLenSq = dot(viewAxis, viewAxis);
    if (viewAxisLenSq <= 0.000001f)
    {
        return false;
    }

    const float3 viewDirection = viewApex * rsqrt(distanceSq);
    const float3 normalizedViewAxis = viewAxis * rsqrt(viewAxisLenSq);
    return dot(viewDirection, normalizedViewAxis) >=
           (bounds.coneCutoff + kConeCutoffEpsilon);
}

void append_or_merge_range(
    uint rangeStart,
    uint rangeEnd,
    inout uint rangeCount,
    inout uint rangeStarts[kMaxRangesPerObject],
    inout uint rangeEnds[kMaxRangesPerObject],
    inout bool rangeOverflow);

bool append_visible_meshlets_in_group(
    uint firstMeshlet,
    uint meshletCount,
    uint groupIndex,
    uint rangeStartIndex,
    Transform transform,
    float radiusScale,
    inout uint rangeCount,
    inout uint rangeStarts[kMaxRangesPerObject],
    inout uint rangeEnds[kMaxRangesPerObject],
    inout bool rangeOverflow,
    inout uint visibleIndexCount,
    inout uint coneTestedMeshletCount,
    inout uint coneRejectedMeshletCount)
{
    const uint groupFirst = groupIndex * kMeshletsPerGroup;
    const uint groupCount = min(kMeshletsPerGroup, meshletCount - groupFirst);

    bool hasVisibleMeshlet = false;
    [loop]
    for (uint i = 0u; i < groupCount; ++i)
    {
        const MeshletBounds bounds =
            g_meshletBounds[firstMeshlet + groupFirst + i];
        ++coneTestedMeshletCount;
        if (is_meshlet_backfacing(bounds, transform, radiusScale))
        {
            ++coneRejectedMeshletCount;
            continue;
        }

        const uint meshletRangeStart = rangeStartIndex + bounds.firstIndex;
        const uint meshletRangeEnd = meshletRangeStart + bounds.indexCount;
        append_or_merge_range(meshletRangeStart, meshletRangeEnd, rangeCount,
                              rangeStarts, rangeEnds, rangeOverflow);
        visibleIndexCount += bounds.indexCount;
        hasVisibleMeshlet = true;
    }
    return hasVisibleMeshlet;
}

void publish_object_group_stats(
    uint testedGroupCount,
    uint frustumRejectedGroupCount,
    uint coneTestedMeshletCount,
    uint coneRejectedMeshletCount,
    uint visibleGroupCount)
{
    if (testedGroupCount != 0u)
    {
        g_stats.InterlockedAdd(kStatsTestedGroupCount, testedGroupCount);
    }
    if (frustumRejectedGroupCount != 0u)
    {
        g_stats.InterlockedAdd(kStatsFrustumRejectedGroupCount,
                               frustumRejectedGroupCount);
    }
    if (coneTestedMeshletCount != 0u)
    {
        g_stats.InterlockedAdd(kStatsConeTestedMeshletCount,
                               coneTestedMeshletCount);
    }
    if (coneRejectedMeshletCount != 0u)
    {
        g_stats.InterlockedAdd(kStatsConeRejectedMeshletCount,
                               coneRejectedMeshletCount);
    }
    if (visibleGroupCount != 0u)
    {
        g_stats.InterlockedAdd(kStatsVisibleGroupCount, visibleGroupCount);
    }
}

void append_or_merge_range(
    uint rangeStart,
    uint rangeEnd,
    inout uint rangeCount,
    inout uint rangeStarts[kMaxRangesPerObject],
    inout uint rangeEnds[kMaxRangesPerObject],
    inout bool rangeOverflow)
{
    if (rangeStart >= rangeEnd)
    {
        return;
    }

    if (rangeCount > 0u)
    {
        const uint lastIndex = rangeCount - 1u;
        if (rangeStart <= rangeEnds[lastIndex] + kMaxMergeGapIndices)
        {
            rangeEnds[lastIndex] = max(rangeEnds[lastIndex], rangeEnd);
            return;
        }
    }

    if (rangeCount >= kMaxRangesPerObject)
    {
        rangeOverflow = true;
        return;
    }

    rangeStarts[rangeCount] = rangeStart;
    rangeEnds[rangeCount] = rangeEnd;
    ++rangeCount;
}

bool emit_range_commands(
    uint objectIndex,
    int baseVertex,
    uint rangeCount,
    uint rangeStarts[kMaxRangesPerObject],
    uint rangeEnds[kMaxRangesPerObject])
{
    if (rangeCount == 0u)
    {
        return true;
    }

    uint commandBase = 0u;
    [loop]
    for (;;)
    {
        const uint currentCount = g_rangeCommandCount.Load(0);
        if (currentCount > g_maxRangeCommandCount ||
            rangeCount > g_maxRangeCommandCount - currentCount)
        {
            return false;
        }

        uint previousCount = 0u;
        g_rangeCommandCount.InterlockedCompareExchange(
            0, currentCount, currentCount + rangeCount, previousCount);
        if (previousCount == currentCount)
        {
            commandBase = currentCount;
            break;
        }
    }

    [loop]
    for (uint i = 0u; i < rangeCount; ++i)
    {
        const uint commandIndex = commandBase + i;
        IndirectCommand command;
        command.drawObjectStartIndex = objectIndex;
        command.indexCountPerInstance = rangeEnds[i] - rangeStarts[i];
        command.instanceCount = 1u;
        command.startIndexLocation = rangeStarts[i];
        command.baseVertexLocation = baseVertex;
        command.startInstanceLocation = 0u;
        g_rangeCommands[commandIndex] = command;
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
        g_stats.Store(kStatsVisibleObjectCount, visibleObjectCount);
        g_stats.Store(
            kStatsSettings,
            (kEnableMeshletRangeDraws ? 1u : 0u) |
                (kEnableConeCulling ? 2u : 0u));
    }

    if (objectIndex >= visibleObjectCount || objectIndex >= g_objectCount)
    {
        return;
    }

    g_objectDrawModes[objectIndex] = kDrawModeNormal;
    if (!kEnableMeshletRangeDraws)
    {
        return;
    }

    const RenderObject renderObject = g_renderObjects[objectIndex];
    const MeshRange meshRange = g_meshRanges[renderObject.meshId];
    const MeshChunkRange chunkRange = g_meshChunkRanges[renderObject.meshId];
    if ((renderObject.padding & kRenderObjectFlagForwardFallback) != 0u ||
        (renderObject.drawFlags & 1u) != 0u ||
        meshRange.meshletCount < (kMeshletsPerGroup * 2u) ||
        meshRange.rangeIndexCount == 0u ||
        chunkRange.chunkCount == 0u ||
        chunkRange.chunkCount > kMaxChunkGroupsPerObjectForRange)
    {
        return;
    }

    // Meshlet range draws break instance batching, so keep mid/far LODs on the
    // ordinary batched path unless the object is large enough on screen.
    if (projected_radius(renderObject.boundsCenterRadius) <
        kMinMeshletRangeProjectedRadius)
    {
        return;
    }

    g_stats.InterlockedAdd(kStatsCandidateObjectCount, 1u);
    g_stats.InterlockedAdd(kStatsTotalIndexCount, meshRange.rangeIndexCount);

    const Transform transform = g_transforms[renderObject.transformId];
    const float radiusScale = transform_radius_scale(transform.worldMatrix);

    uint rangeStarts[kMaxRangesPerObject];
    uint rangeEnds[kMaxRangesPerObject];
    uint rangeCount = 0u;
    uint visibleGroupCount = 0u;
    uint visibleIndexCount = 0u;
    uint rangeDrawnIndexCount = 0u;
    uint testedGroupCount = 0u;
    uint frustumRejectedGroupCount = 0u;
    uint coneTestedMeshletCount = 0u;
    uint coneRejectedMeshletCount = 0u;
    bool rangeOverflow = false;

    [loop]
    for (uint groupIndex = 0u; groupIndex < chunkRange.chunkCount; ++groupIndex)
    {
        ++testedGroupCount;

        const MeshletChunk chunk =
            g_meshletChunks[chunkRange.firstChunk + groupIndex];
        if (chunk.indexCount == 0u)
        {
            continue;
        }

        const float4 worldCenter =
            mul(float4(chunk.boundsCenter, 1.0f), transform.worldMatrix);
        const float4 viewCenter = mul(worldCenter, g_viewMatrix);
        const float worldRadius = chunk.boundsRadius * radiusScale;
        if (!is_view_sphere_inside_frustum(
                viewCenter.xyz,
                worldRadius * kMeshletRangeFrustumRadiusScale))
        {
            ++frustumRejectedGroupCount;
            continue;
        }

        if (!append_visible_meshlets_in_group(
                chunk.firstMeshlet, chunk.meshletCount, 0u,
                meshRange.rangeStartIndex, transform, radiusScale, rangeCount,
                rangeStarts, rangeEnds, rangeOverflow, visibleIndexCount,
                coneTestedMeshletCount,
                coneRejectedMeshletCount))
        {
            continue;
        }

        ++visibleGroupCount;
        if (rangeOverflow)
        {
            break;
        }
    }

    publish_object_group_stats(testedGroupCount, frustumRejectedGroupCount,
                               coneTestedMeshletCount,
                               coneRejectedMeshletCount, visibleGroupCount);

    if (visibleGroupCount == 0u || coneRejectedMeshletCount != 0u)
    {
        g_stats.InterlockedAdd(kStatsFallbackObjectCount, 1u);
        g_objectDrawModes[objectIndex] = kDrawModeFallback;
        return;
    }

    if (rangeOverflow || rangeCount == 0u)
    {
        g_stats.InterlockedAdd(kStatsFallbackObjectCount, 1u);
        g_objectDrawModes[objectIndex] = kDrawModeFallback;
        return;
    }

    [loop]
    for (uint rangeIndex = 0u; rangeIndex < rangeCount; ++rangeIndex)
    {
        rangeDrawnIndexCount += rangeEnds[rangeIndex] - rangeStarts[rangeIndex];
    }

    const bool weakSavings =
        rangeDrawnIndexCount * 100u >=
        meshRange.rangeIndexCount * kMaxRangeDrawnPercent;
    if (weakSavings)
    {
        g_stats.InterlockedAdd(kStatsFallbackObjectCount, 1u);
        g_objectDrawModes[objectIndex] = kDrawModeFallback;
        return;
    }

    if (emit_range_commands(objectIndex, meshRange.baseVertex, rangeCount,
                            rangeStarts, rangeEnds))
    {
        g_stats.InterlockedAdd(kStatsRangeObjectCount, 1u);
        g_stats.InterlockedAdd(kStatsRangeCommandCount, rangeCount);
        g_stats.InterlockedAdd(kStatsRangeIndexCount, rangeDrawnIndexCount);
        g_objectDrawModes[objectIndex] = kDrawModeGroupRange;
    }
    else
    {
        g_stats.InterlockedAdd(kStatsFallbackObjectCount, 1u);
        g_objectDrawModes[objectIndex] = kDrawModeFallback;
    }
}
