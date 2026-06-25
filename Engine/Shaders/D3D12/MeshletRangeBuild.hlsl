// Meshlet visibility を連続 index range にまとめ、Range DrawIndexedIndirect
// command を生成する。描画は通常 IA/VB/IB 経路に任せる。

#define DRAW_PATH_CULLED 0u
#define DRAW_PATH_NORMAL 1u
#define DRAW_PATH_RANGE 2u
#define DRAW_PATH_FALLBACK 3u

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

struct RangeDrawCommand
{
    uint objectIndex;
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

cbuffer MinRangeMeshletCountParam : register(b1)
{
    uint g_minRangeMeshletCount;
};

cbuffer MinRangeIndexCountParam : register(b2)
{
    uint g_minRangeIndexCount;
};

cbuffer MinRangeProjectedRadiusParam : register(b3)
{
    float g_minRangeProjectedRadius;
};

cbuffer MaxRangeDrawCountParam : register(b4)
{
    uint g_maxRangeDrawCount;
};

cbuffer MaxRangesPerObjectParam : register(b5)
{
    uint g_maxRangesPerObject;
};

cbuffer MaxRangeGapIndicesParam : register(b6)
{
    uint g_maxRangeGapIndices;
};

cbuffer ViewProjection : register(b7)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
    float4 g_cameraPosition;
};

cbuffer TileCountXParam : register(b8)
{
    uint g_tileCountX;
};

cbuffer TileCountYParam : register(b9)
{
    uint g_tileCountY;
};

cbuffer TileSizeParam : register(b10)
{
    uint g_tileSize;
};

StructuredBuffer<RenderObject> g_renderObjects : register(t0);
StructuredBuffer<Transform> g_transforms : register(t1);
ByteAddressBuffer g_renderObjectCount : register(t2);
StructuredBuffer<MeshRange> g_meshRanges : register(t3);
StructuredBuffer<MeshletBounds> g_meshletBounds : register(t4);
StructuredBuffer<uint> g_refinedVisibility : register(t5);
ByteAddressBuffer g_hizDepth : register(t6);

RWStructuredBuffer<uint> g_objectDrawPath : register(u0);
RWStructuredBuffer<RangeDrawCommand> g_rangeDrawCommands : register(u1);
RWByteAddressBuffer g_rangeDrawCount : register(u2);
RWByteAddressBuffer g_rangeOverflow : register(u3);
RWByteAddressBuffer g_rangeStats : register(u4);

static const uint k_statsCandidateObjectCount = 0u;
static const uint k_statsRangeDrawObjectCount = 1u;
static const uint k_statsNormalDrawObjectCount = 2u;
static const uint k_statsCulledObjectCount = 3u;
static const uint k_statsFallbackObjectCount = 4u;
static const uint k_statsRangeCommandCount = 5u;
static const uint k_statsOverflow = 6u;
static const uint k_statsTestedMeshletCount = 7u;
static const uint k_statsVisibleMeshletCount = 8u;
static const uint k_statsCulledMeshletCount = 9u;
static const uint k_statsVisibleIndexCount = 10u;
static const uint k_statsRangeDrawnIndexCount = 11u;
static const uint k_statsRangeCulledIndexCount = 12u;
static const uint k_statsRangeExtraGapIndexCount = 13u;
static const uint k_statsFrustumCulledMeshletCount = 14u;
static const uint k_statsHiZCulledMeshletCount = 15u;
static const uint k_statsBackfaceCulledMeshletCount = 16u;
static const float k_minRangeWorldRadius = 0.0f;

void stats_add(uint index, uint value)
{
    uint originalValue;
    g_rangeStats.InterlockedAdd(index * 4u, value, originalValue);
}

void stats_store(uint index, uint value)
{
    g_rangeStats.Store(index * 4u, value);
}

bool is_sphere_inside_plane(float4 plane, float3 center, float radius)
{
    const float invPlaneLength =
        rsqrt(max(dot(plane.xyz, plane.xyz), 0.000000000001f));
    const float signedDistance =
        (dot(plane.xyz, center) + plane.w) * invPlaneLength;
    return signedDistance >= -radius;
}

bool is_sphere_inside_frustum(float3 worldCenter, float radius)
{
    if (radius <= 0.0f)
    {
        return false;
    }

    const float4 viewCenter = mul(float4(worldCenter, 1.0f), g_viewMatrix);

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
            projectionColumn3 + projectionColumn0, viewCenter.xyz, radius))
    {
        return false;
    }
    if (!is_sphere_inside_plane(
            projectionColumn3 - projectionColumn0, viewCenter.xyz, radius))
    {
        return false;
    }
    if (!is_sphere_inside_plane(
            projectionColumn3 + projectionColumn1, viewCenter.xyz, radius))
    {
        return false;
    }
    if (!is_sphere_inside_plane(
            projectionColumn3 - projectionColumn1, viewCenter.xyz, radius))
    {
        return false;
    }
    if (!is_sphere_inside_plane(
            projectionColumn3 + projectionColumn2, viewCenter.xyz, radius))
    {
        return false;
    }
    if (!is_sphere_inside_plane(
            projectionColumn3 - projectionColumn2, viewCenter.xyz, radius))
    {
        return false;
    }
    return true;
}

uint quantize_depth(float depth)
{
    return (uint)min(max(depth, 0.0f) * 4294967295.0f, 4294967295.0f);
}

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

bool project_sphere_to_hiz_tiles(
    float3 worldCenter,
    float radius,
    out uint2 minTile,
    out uint2 maxTile,
    out uint nearDepth)
{
    minTile = uint2(0u, 0u);
    maxTile = uint2(0u, 0u);
    nearDepth = 0u;
    if (g_tileCountX == 0u || g_tileCountY == 0u || g_tileSize == 0u)
    {
        return false;
    }

    const float4 viewCenter = mul(float4(worldCenter, 1.0f), g_viewMatrix);
    const float viewZ = viewCenter.z;
    const float4 clipCenter = mul(viewCenter, g_projectionMatrix);
    if (radius <= 0.0f || viewZ - radius <= 0.001f ||
        abs(clipCenter.w) <= 0.000001f)
    {
        return false;
    }

    const float projection00 = max(abs(g_projectionMatrix[0][0]), 0.000001f);
    const float projection11 = max(abs(g_projectionMatrix[1][1]), 0.000001f);
    const float2 ndcCenter = clipCenter.xy / clipCenter.w;
    const float2 projectedRadius =
        float2(radius * projection00, radius * projection11) /
        max(viewZ, 0.000001f);
    const float2 minNdc = max(ndcCenter - projectedRadius, float2(-1.0f, -1.0f));
    const float2 maxNdc = min(ndcCenter + projectedRadius, float2(1.0f, 1.0f));
    const float2 screenSize =
        float2((float)(g_tileCountX * g_tileSize),
               (float)(g_tileCountY * g_tileSize));
    const float2 minPixel = (minNdc * float2(0.5f, -0.5f) + 0.5f) * screenSize;
    const float2 maxPixel = (maxNdc * float2(0.5f, -0.5f) + 0.5f) * screenSize;
    const float2 rectMin = min(minPixel, maxPixel);
    const float2 rectMax = max(minPixel, maxPixel);

    minTile = min((uint2)floor(rectMin / (float)g_tileSize),
                  uint2(g_tileCountX - 1u, g_tileCountY - 1u));
    maxTile = min((uint2)floor(rectMax / (float)g_tileSize),
                  uint2(g_tileCountX - 1u, g_tileCountY - 1u));
    nearDepth =
        quantize_depth(project_device_depth(max(viewZ - radius, 0.001f)));
    return true;
}

bool is_occluded_by_hiz(float3 worldCenter, float radius)
{
    uint2 minTile;
    uint2 maxTile;
    uint nearDepth;
    if (!project_sphere_to_hiz_tiles(
            worldCenter, radius, minTile, maxTile, nearDepth))
    {
        return false;
    }

    const uint depthBias = 8589934u;
    for (uint tileY = minTile.y; tileY <= maxTile.y; ++tileY)
    {
        for (uint tileX = minTile.x; tileX <= maxTile.x; ++tileX)
        {
            const uint tileIndex = tileY * g_tileCountX + tileX;
            const uint tileMaxDepth = g_hizDepth.Load(tileIndex * 4u);
            if (tileMaxDepth == 0u ||
                nearDepth <= tileMaxDepth ||
                nearDepth - tileMaxDepth <= depthBias)
            {
                return false;
            }
        }
    }
    return true;
}

float transform_max_scale(Transform transform)
{
    return max(length(transform.worldMatrix[0].xyz),
               max(length(transform.worldMatrix[1].xyz),
                   length(transform.worldMatrix[2].xyz)));
}

bool is_meshlet_backfacing(MeshletBounds meshlet, Transform transform)
{
    if ((meshlet.flags & 1u) == 0u || meshlet.coneCutoff <= -0.9999f)
    {
        return false;
    }

    float3 worldAxis =
        mul(float4(meshlet.coneAxis, 0.0f), transform.normalMatrix).xyz;
    const float axisLengthSq = dot(worldAxis, worldAxis);
    if (axisLengthSq <= 0.000001f)
    {
        return false;
    }
    worldAxis *= rsqrt(axisLengthSq);

    const float3 worldApex =
        mul(float4(meshlet.coneApex, 1.0f), transform.worldMatrix).xyz;
    const float3 viewVector = worldApex - g_cameraPosition.xyz;
    const float viewLengthSq = dot(viewVector, viewVector);
    if (viewLengthSq <= 0.000001f)
    {
        return false;
    }

    const float3 viewDirection = viewVector * rsqrt(viewLengthSq);
    return dot(viewDirection, worldAxis) >= meshlet.coneCutoff;
}

float object_projected_radius(RenderObject renderObject)
{
    const float4 viewCenter =
        mul(float4(renderObject.boundsCenterRadius.xyz, 1.0f), g_viewMatrix);
    return renderObject.boundsCenterRadius.w *
        abs(g_projectionMatrix[1][1]) /
        max(viewCenter.z, 0.001f);
}

bool should_use_range_rendering(RenderObject renderObject, MeshRange meshRange)
{
    if ((renderObject.drawFlags & 1u) != 0u)
    {
        return false;
    }
    if (renderObject.skinPaletteOffset != 0xffffffffu)
    {
        return false;
    }
    if (meshRange.meshletCount < g_minRangeMeshletCount)
    {
        return false;
    }
    if (meshRange.rangeIndexCount < g_minRangeIndexCount)
    {
        return false;
    }
    if (renderObject.boundsCenterRadius.w < k_minRangeWorldRadius)
    {
        return false;
    }
    if (object_projected_radius(renderObject) < g_minRangeProjectedRadius)
    {
        return false;
    }
    return true;
}

void stats_add_if_nonzero(uint index, uint value)
{
    if (value != 0u)
    {
        stats_add(index, value);
    }
}

void flush_meshlet_stats(
    uint testedMeshletCount,
    uint visibleMeshletCount,
    uint culledMeshletCount,
    uint frustumCulledMeshletCount,
    uint backfaceCulledMeshletCount,
    uint hiZCulledMeshletCount)
{
    stats_add_if_nonzero(k_statsTestedMeshletCount, testedMeshletCount);
    stats_add_if_nonzero(k_statsVisibleMeshletCount, visibleMeshletCount);
    stats_add_if_nonzero(k_statsCulledMeshletCount, culledMeshletCount);
    stats_add_if_nonzero(
        k_statsFrustumCulledMeshletCount,
        frustumCulledMeshletCount);
    stats_add_if_nonzero(
        k_statsBackfaceCulledMeshletCount,
        backfaceCulledMeshletCount);
    stats_add_if_nonzero(k_statsHiZCulledMeshletCount, hiZCulledMeshletCount);
}

void emit_range(uint objectIndex, MeshRange meshRange, uint startIndex, uint endIndex)
{
    if (endIndex <= startIndex)
    {
        return;
    }

    uint commandIndex = 0u;
    g_rangeDrawCount.InterlockedAdd(0, 1u, commandIndex);
    if (commandIndex >= g_maxRangeDrawCount)
    {
        g_rangeOverflow.Store(0, 1u);
        stats_store(k_statsOverflow, 1u);
        return;
    }

    RangeDrawCommand command;
    command.objectIndex = objectIndex;
    command.indexCountPerInstance = endIndex - startIndex;
    command.instanceCount = 1u;
    command.startIndexLocation = startIndex;
    command.baseVertexLocation = meshRange.baseVertex;
    command.startInstanceLocation = 0u;
    g_rangeDrawCommands[commandIndex] = command;
    stats_add(k_statsRangeCommandCount, 1u);
    stats_add(k_statsRangeDrawnIndexCount, endIndex - startIndex);
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint objectIndex = dispatchThreadId.x;
    const uint visibleObjectCount = g_renderObjectCount.Load(0);
    if (objectIndex >= visibleObjectCount || objectIndex >= g_objectCount)
    {
        return;
    }

    if (g_refinedVisibility[objectIndex] == 0u)
    {
        g_objectDrawPath[objectIndex] = DRAW_PATH_CULLED;
        stats_add(k_statsCulledObjectCount, 1u);
        return;
    }

    const RenderObject renderObject = g_renderObjects[objectIndex];
    const MeshRange meshRange = g_meshRanges[renderObject.meshId];
    if (!should_use_range_rendering(renderObject, meshRange))
    {
        g_objectDrawPath[objectIndex] = DRAW_PATH_NORMAL;
        stats_add(k_statsNormalDrawObjectCount, 1u);
        return;
    }
    stats_add(k_statsCandidateObjectCount, 1u);

    static const uint k_meshletsPerRangeGroup = 2u;
    static const uint k_localMaxRanges = 128u;
    uint rangeStarts[k_localMaxRanges];
    uint rangeEnds[k_localMaxRanges];
    float rangeDepths[k_localMaxRanges];

    const Transform transform = g_transforms[renderObject.transformId];
    const float maxScale = transform_max_scale(transform);
    const uint meshletEnd = meshRange.firstMeshlet + meshRange.meshletCount;
    const uint clampedMaxRanges =
        min(max(g_maxRangesPerObject, 1u), k_localMaxRanges);

    bool anyVisible = false;
    uint firstVisibleIndex = 0xffffffffu;
    uint lastVisibleIndex = 0u;
    uint rangeCount = 0u;
    uint visibleIndexCount = 0u;
    uint testedMeshletCount = 0u;
    uint visibleMeshletCount = 0u;
    uint culledMeshletCount = 0u;
    uint frustumCulledMeshletCount = 0u;
    uint backfaceCulledMeshletCount = 0u;
    uint hiZCulledMeshletCount = 0u;

    for (uint groupStartMeshlet = meshRange.firstMeshlet;
         groupStartMeshlet < meshletEnd;
         groupStartMeshlet += k_meshletsPerRangeGroup)
    {
        const uint groupEndMeshlet =
            min(groupStartMeshlet + k_meshletsPerRangeGroup, meshletEnd);
        bool groupVisible = false;
        bool groupHasDrawableMeshlet = false;
        uint groupStartIndex = 0xffffffffu;
        uint groupEndIndex = 0u;
        float groupDepth = 0.0f;

        for (uint meshletIndex = groupStartMeshlet;
             meshletIndex < groupEndMeshlet;
             ++meshletIndex)
        {
            const MeshletBounds meshlet = g_meshletBounds[meshletIndex];
            if (meshlet.indexCount == 0u)
            {
                continue;
            }

            groupHasDrawableMeshlet = true;
            groupStartIndex = min(groupStartIndex, meshlet.firstIndex);
            groupEndIndex =
                max(groupEndIndex, meshlet.firstIndex + meshlet.indexCount);
            ++testedMeshletCount;

            const float3 worldCenter =
                mul(float4(meshlet.center, 1.0f), transform.worldMatrix).xyz;
            const float worldRadius = meshlet.radius * maxScale;
            const bool insideFrustum =
                is_sphere_inside_frustum(worldCenter, worldRadius);
            const bool backfacing =
                insideFrustum && is_meshlet_backfacing(meshlet, transform);
            const bool occludedByHiZ =
                insideFrustum && !backfacing &&
                is_occluded_by_hiz(worldCenter, worldRadius);
            const bool visible = insideFrustum && !backfacing && !occludedByHiZ;
            const float viewDepth =
                mul(float4(worldCenter, 1.0f), g_viewMatrix).z - worldRadius;

            if (visible)
            {
                anyVisible = true;
                const bool wasGroupVisible = groupVisible;
                groupVisible = true;
                groupDepth = wasGroupVisible ? min(groupDepth, viewDepth) : viewDepth;
                const uint start = meshlet.firstIndex;
                const uint end = meshlet.firstIndex + meshlet.indexCount;
                firstVisibleIndex = min(firstVisibleIndex, start);
                lastVisibleIndex = max(lastVisibleIndex, end);
                visibleIndexCount += meshlet.indexCount;
                ++visibleMeshletCount;
            }
            else
            {
                ++culledMeshletCount;
                if (!insideFrustum)
                {
                    ++frustumCulledMeshletCount;
                }
                else if (backfacing)
                {
                    ++backfaceCulledMeshletCount;
                }
                else if (occludedByHiZ)
                {
                    ++hiZCulledMeshletCount;
                }
            }
        }

        if (!groupVisible || !groupHasDrawableMeshlet)
        {
            continue;
        }

        if (rangeCount != 0u)
        {
            const uint previousRange = rangeCount - 1u;
            const uint gap =
                groupStartIndex > rangeEnds[previousRange]
                    ? groupStartIndex - rangeEnds[previousRange]
                    : 0u;
            if (gap <= g_maxRangeGapIndices)
            {
                rangeEnds[previousRange] =
                    max(rangeEnds[previousRange], groupEndIndex);
                rangeDepths[previousRange] =
                    min(rangeDepths[previousRange], groupDepth);
                continue;
            }
        }

        if (rangeCount >= k_localMaxRanges)
        {
            uint bestMergeIndex = 0u;
            uint bestMergeGap = 0xffffffffu;
            for (uint mergeIndex = 0u; mergeIndex + 1u < rangeCount;
                 ++mergeIndex)
            {
                const uint gap =
                    rangeStarts[mergeIndex + 1u] > rangeEnds[mergeIndex]
                        ? rangeStarts[mergeIndex + 1u] - rangeEnds[mergeIndex]
                        : 0u;
                if (gap < bestMergeGap)
                {
                    bestMergeGap = gap;
                    bestMergeIndex = mergeIndex;
                }
            }
            rangeEnds[bestMergeIndex] =
                max(rangeEnds[bestMergeIndex], rangeEnds[bestMergeIndex + 1u]);
            rangeDepths[bestMergeIndex] =
                min(rangeDepths[bestMergeIndex],
                    rangeDepths[bestMergeIndex + 1u]);
            for (uint shiftIndex = bestMergeIndex + 1u;
                 shiftIndex + 1u < rangeCount;
                 ++shiftIndex)
            {
                rangeStarts[shiftIndex] = rangeStarts[shiftIndex + 1u];
                rangeEnds[shiftIndex] = rangeEnds[shiftIndex + 1u];
                rangeDepths[shiftIndex] = rangeDepths[shiftIndex + 1u];
            }
            --rangeCount;
        }

        rangeStarts[rangeCount] = groupStartIndex;
        rangeEnds[rangeCount] = groupEndIndex;
        rangeDepths[rangeCount] = groupDepth;
        ++rangeCount;
    }

    flush_meshlet_stats(
        testedMeshletCount,
        visibleMeshletCount,
        culledMeshletCount,
        frustumCulledMeshletCount,
        backfaceCulledMeshletCount,
        hiZCulledMeshletCount);

    if (!anyVisible)
    {
        g_objectDrawPath[objectIndex] = DRAW_PATH_CULLED;
        stats_add(k_statsCulledObjectCount, 1u);
        return;
    }
    stats_add(k_statsVisibleIndexCount, visibleIndexCount);

    while (rangeCount > clampedMaxRanges)
    {
        uint bestMergeIndex = 0u;
        uint bestMergeGap = 0xffffffffu;
        for (uint mergeIndex = 0u; mergeIndex + 1u < rangeCount; ++mergeIndex)
        {
            const uint gap =
                rangeStarts[mergeIndex + 1u] > rangeEnds[mergeIndex]
                    ? rangeStarts[mergeIndex + 1u] - rangeEnds[mergeIndex]
                    : 0u;
            if (gap < bestMergeGap)
            {
                bestMergeGap = gap;
                bestMergeIndex = mergeIndex;
            }
        }
        rangeEnds[bestMergeIndex] =
            max(rangeEnds[bestMergeIndex], rangeEnds[bestMergeIndex + 1u]);
        rangeDepths[bestMergeIndex] =
            min(rangeDepths[bestMergeIndex], rangeDepths[bestMergeIndex + 1u]);
        for (uint shiftIndex = bestMergeIndex + 1u;
             shiftIndex + 1u < rangeCount;
             ++shiftIndex)
        {
            rangeStarts[shiftIndex] = rangeStarts[shiftIndex + 1u];
            rangeEnds[shiftIndex] = rangeEnds[shiftIndex + 1u];
            rangeDepths[shiftIndex] = rangeDepths[shiftIndex + 1u];
        }
        --rangeCount;
    }

    uint emittedIndexCount = 0u;
    for (uint sortIndex = 0u; sortIndex < rangeCount; ++sortIndex)
    {
        for (uint compareIndex = sortIndex + 1u;
             compareIndex < rangeCount;
             ++compareIndex)
        {
            if (rangeDepths[compareIndex] < rangeDepths[sortIndex])
            {
                const uint startSwap = rangeStarts[sortIndex];
                const uint endSwap = rangeEnds[sortIndex];
                const float depthSwap = rangeDepths[sortIndex];
                rangeStarts[sortIndex] = rangeStarts[compareIndex];
                rangeEnds[sortIndex] = rangeEnds[compareIndex];
                rangeDepths[sortIndex] = rangeDepths[compareIndex];
                rangeStarts[compareIndex] = startSwap;
                rangeEnds[compareIndex] = endSwap;
                rangeDepths[compareIndex] = depthSwap;
            }
        }
    }

    for (uint rangeIndex = 0u; rangeIndex < rangeCount; ++rangeIndex)
    {
        emittedIndexCount += rangeEnds[rangeIndex] - rangeStarts[rangeIndex];
    }

    const bool drawsAlmostFullMesh =
        emittedIndexCount >=
        meshRange.rangeIndexCount - (meshRange.rangeIndexCount / 8u);
    const bool gapOverdrawTooLarge =
        visibleIndexCount != 0u &&
        emittedIndexCount > visibleIndexCount * 3u;
    if (drawsAlmostFullMesh || gapOverdrawTooLarge)
    {
        stats_add(k_statsFallbackObjectCount, 1u);
        g_objectDrawPath[objectIndex] = DRAW_PATH_FALLBACK;
        stats_add(k_statsNormalDrawObjectCount, 1u);
        return;
    }

    for (uint rangeIndex = 0u; rangeIndex < rangeCount; ++rangeIndex)
    {
        emit_range(
            objectIndex,
            meshRange,
            rangeStarts[rangeIndex],
            rangeEnds[rangeIndex]);
    }
    if (emittedIndexCount > visibleIndexCount)
    {
        stats_add(
            k_statsRangeExtraGapIndexCount,
            emittedIndexCount - visibleIndexCount);
    }

    g_objectDrawPath[objectIndex] = DRAW_PATH_RANGE;
    stats_add(k_statsRangeDrawObjectCount, 1u);
    if (meshRange.rangeIndexCount > emittedIndexCount)
    {
        stats_add(
            k_statsRangeCulledIndexCount,
            meshRange.rangeIndexCount - emittedIndexCount);
    }
}
