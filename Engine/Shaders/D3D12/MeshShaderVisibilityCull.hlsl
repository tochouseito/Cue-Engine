// Expand coarse visible chunks into a visible meshlet list.

struct CandidateChunk
{
    uint objectIndex;
    uint meshId;
    uint firstMeshlet;
    uint meshletCountAndSegmentCount;
};

struct VisibleMeshlet
{
    uint objectIndex;
    uint meshId;
    uint meshletIndex;
    uint segmentStartIndex;
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
    uint visibilityTriangleStart;
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

cbuffer MaxVisibleMeshletParam : register(b0)
{
    uint g_maxVisibleMeshletCount;
};

cbuffer ViewProjection : register(b1)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
};

cbuffer HiZWidthParam : register(b2)
{
    uint g_hizWidth;
};

cbuffer HiZHeightParam : register(b3)
{
    uint g_hizHeight;
};

cbuffer OcclusionParam : register(b4)
{
    uint g_occlusionEnabled;
};

StructuredBuffer<CandidateChunk> g_candidateChunks : register(t0);
StructuredBuffer<MeshRange> g_meshRanges : register(t1);
StructuredBuffer<MeshletBounds> g_meshletBounds : register(t2);
StructuredBuffer<RenderObject> g_renderObjects : register(t3);
StructuredBuffer<Transform> g_transforms : register(t4);
Texture2D<uint> g_occlusionHiZ : register(t5);

RWStructuredBuffer<VisibleMeshlet> g_visibleMeshlets : register(u0);
RWByteAddressBuffer g_counters : register(u1);

static const uint kVisibleMeshletCounterOffset = 0u;
static const uint kCandidateChunkCounterOffset = 4u;
static const uint kTestedMeshletCounterOffset = 8u;
static const uint kFrustumRejectedMeshletCounterOffset = 12u;
static const uint kConeRejectedMeshletCounterOffset = 16u;
static const uint kOcclusionTestedMeshletCounterOffset = 20u;
static const uint kOcclusionRejectedMeshletCounterOffset = 24u;
static const uint kVisibleMeshletPrimitiveCounterOffset = 28u;
static const uint kMaxMeshletsPerChunk = 8u;
static const uint kMaxOutputIndicesPerSegment = 384u;
static const float kConeCutoffEpsilon = 0.0001f;
static const float kOcclusionDepthBias = 0.0015f;
static const uint kMaxOcclusionRectTexels = 20u;

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

bool is_meshlet_backfacing(MeshletBounds bounds,
                           Transform transform,
                           float radiusScale)
{
    if ((bounds.flags & 1u) == 0u || bounds.coneCutoff <= 0.0f)
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

float decode_hiz_depth(uint depth)
{
    return (float)depth * (1.0f / 4294967295.0f);
}

bool is_meshlet_occluded_by_hiz(float3 viewCenter, float radius)
{
    if (g_occlusionEnabled == 0u || g_hizWidth == 0u || g_hizHeight == 0u)
    {
        return false;
    }

    const float viewZ = viewCenter.z;
    if (viewZ <= radius + 0.001f || radius <= 0.0f)
    {
        return false;
    }

    const float4 clipCenter = mul(float4(viewCenter, 1.0f), g_projectionMatrix);
    if (abs(clipCenter.w) <= 0.000001f)
    {
        return false;
    }

    const float2 ndc = clipCenter.xy / clipCenter.w;
    const float projectedRadiusNdc =
        radius * abs(g_projectionMatrix[1][1]) / max(viewZ, 0.001f);
    const float2 minNdc = clamp(ndc - projectedRadiusNdc, -1.0f, 1.0f);
    const float2 maxNdc = clamp(ndc + projectedRadiusNdc, -1.0f, 1.0f);
    if (maxNdc.x <= -1.0f || minNdc.x >= 1.0f ||
        maxNdc.y <= -1.0f || minNdc.y >= 1.0f)
    {
        return false;
    }

    const float2 hizSize = float2((float)g_hizWidth, (float)g_hizHeight);
    const uint2 p0 =
        min((uint2)(((minNdc * float2(0.5f, -0.5f) +
                      float2(0.5f, 0.5f)) *
                     hizSize)),
            uint2(g_hizWidth - 1u, g_hizHeight - 1u));
    const uint2 p1 =
        min((uint2)(((maxNdc * float2(0.5f, -0.5f) +
                      float2(0.5f, 0.5f)) *
                     hizSize)),
            uint2(g_hizWidth - 1u, g_hizHeight - 1u));

    const uint minX = min(p0.x, p1.x);
    const uint maxX = max(p0.x, p1.x);
    const uint minY = min(p0.y, p1.y);
    const uint maxY = max(p0.y, p1.y);
    const uint rectWidth = maxX - minX + 1u;
    const uint rectHeight = maxY - minY + 1u;
    if (rectWidth > kMaxOcclusionRectTexels ||
        rectHeight > kMaxOcclusionRectTexels)
    {
        return false;
    }

    const uint2 center = uint2((minX + maxX) >> 1u, (minY + maxY) >> 1u);
    const uint quarterX0 = (minX + center.x) >> 1u;
    const uint quarterX1 = (maxX + center.x + 1u) >> 1u;
    const uint quarterY0 = (minY + center.y) >> 1u;
    const uint quarterY1 = (maxY + center.y + 1u) >> 1u;
    const uint2 samples[9] =
    {
        center,
        uint2(quarterX0, center.y),
        uint2(quarterX1, center.y),
        uint2(center.x, quarterY0),
        uint2(center.x, quarterY1),
        uint2(quarterX0, quarterY0),
        uint2(quarterX1, quarterY0),
        uint2(quarterX0, quarterY1),
        uint2(quarterX1, quarterY1)
    };

    const float meshletNearDepth =
        project_device_depth(max(viewZ - radius, 0.001f));
    [unroll]
    for (uint sampleIndex = 0u; sampleIndex < 9u; ++sampleIndex)
    {
        const float occluderDepth =
            decode_hiz_depth(g_occlusionHiZ.Load(int3(samples[sampleIndex], 0)));
        if (meshletNearDepth <= occluderDepth + kOcclusionDepthBias)
        {
            return false;
        }
    }

    return true;
}

bool is_meshlet_visible(MeshletBounds bounds,
                        Transform transform,
                        float radiusScale)
{
    const float4 worldCenter =
        mul(float4(bounds.center, 1.0f), transform.worldMatrix);
    const float3 viewCenter = mul(worldCenter, g_viewMatrix).xyz;
    if (!is_view_sphere_inside_frustum(viewCenter,
                                       bounds.radius * radiusScale * 1.05f))
    {
        return false;
    }
    if (is_meshlet_backfacing(bounds, transform, radiusScale))
    {
        return false;
    }
    if (is_meshlet_occluded_by_hiz(viewCenter, bounds.radius * radiusScale))
    {
        return false;
    }
    return true;
}

bool is_meshlet_visible_with_stats(MeshletBounds bounds,
                                   Transform transform,
                                   float radiusScale)
{
    g_counters.InterlockedAdd(kTestedMeshletCounterOffset, 1u);

    const float4 worldCenter =
        mul(float4(bounds.center, 1.0f), transform.worldMatrix);
    const float3 viewCenter = mul(worldCenter, g_viewMatrix).xyz;
    const float viewRadius = bounds.radius * radiusScale;
    if (!is_view_sphere_inside_frustum(viewCenter, viewRadius * 1.05f))
    {
        g_counters.InterlockedAdd(kFrustumRejectedMeshletCounterOffset, 1u);
        return false;
    }
    if (is_meshlet_backfacing(bounds, transform, radiusScale))
    {
        g_counters.InterlockedAdd(kConeRejectedMeshletCounterOffset, 1u);
        return false;
    }
    if (g_occlusionEnabled != 0u)
    {
        g_counters.InterlockedAdd(kOcclusionTestedMeshletCounterOffset, 1u);
        if (is_meshlet_occluded_by_hiz(viewCenter, viewRadius))
        {
            g_counters.InterlockedAdd(kOcclusionRejectedMeshletCounterOffset,
                                      1u);
            return false;
        }
    }

    g_counters.InterlockedAdd(kVisibleMeshletPrimitiveCounterOffset, 1u);
    return true;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint candidateChunkCount =
        g_counters.Load(kCandidateChunkCounterOffset);
    if (dispatchThreadId.x >= candidateChunkCount)
    {
        return;
    }

    const CandidateChunk candidate = g_candidateChunks[dispatchThreadId.x];
    const MeshRange meshRange = g_meshRanges[candidate.meshId];
    const RenderObject renderObject = g_renderObjects[candidate.objectIndex];
    const Transform transform = g_transforms[renderObject.transformId];
    const float radiusScale = transform_radius_scale(transform.worldMatrix);
    const uint candidateMeshletCount =
        min(candidate.meshletCountAndSegmentCount & 0xffffu,
            kMaxMeshletsPerChunk);
    uint visibleSegmentCount = 0u;
    [unroll]
    for (uint meshletOffset = 0u; meshletOffset < kMaxMeshletsPerChunk;
         ++meshletOffset)
    {
        if (meshletOffset >= candidateMeshletCount)
        {
            break;
        }

        const uint meshletIndex = candidate.firstMeshlet + meshletOffset;
        if (meshletIndex >= meshRange.firstMeshlet &&
            meshletIndex < meshRange.firstMeshlet + meshRange.meshletCount)
        {
            const MeshletBounds bounds = g_meshletBounds[meshletIndex];
            if (!is_meshlet_visible_with_stats(bounds, transform, radiusScale))
            {
                continue;
            }
            visibleSegmentCount +=
                (bounds.indexCount + kMaxOutputIndicesPerSegment - 1u) /
                kMaxOutputIndicesPerSegment;
        }
    }

    if (visibleSegmentCount == 0u)
    {
        return;
    }

    uint baseVisibleIndex = 0u;
    g_counters.InterlockedAdd(kVisibleMeshletCounterOffset, visibleSegmentCount,
                              baseVisibleIndex);
    if (baseVisibleIndex >= g_maxVisibleMeshletCount)
    {
        return;
    }

    const uint writableSegmentCount =
        min(visibleSegmentCount, g_maxVisibleMeshletCount - baseVisibleIndex);
    uint writtenSegmentCount = 0u;
    [unroll]
    for (uint meshletOffset = 0u; meshletOffset < kMaxMeshletsPerChunk;
         ++meshletOffset)
    {
        if (meshletOffset >= candidateMeshletCount ||
            writtenSegmentCount >= writableSegmentCount)
        {
            return;
        }

        const uint meshletIndex = candidate.firstMeshlet + meshletOffset;
        if (meshletIndex < meshRange.firstMeshlet ||
            meshletIndex >= meshRange.firstMeshlet + meshRange.meshletCount)
        {
            continue;
        }

        const MeshletBounds bounds = g_meshletBounds[meshletIndex];
        if (!is_meshlet_visible(bounds, transform, radiusScale))
        {
            continue;
        }
        for (uint segmentStartIndex = 0u; segmentStartIndex < bounds.indexCount;
             segmentStartIndex += kMaxOutputIndicesPerSegment)
        {
            if (writtenSegmentCount >= writableSegmentCount)
            {
                return;
            }

            VisibleMeshlet visibleMeshlet;
            visibleMeshlet.objectIndex = candidate.objectIndex;
            visibleMeshlet.meshId = candidate.meshId;
            visibleMeshlet.meshletIndex = meshletIndex;
            visibleMeshlet.segmentStartIndex = segmentStartIndex;
            g_visibleMeshlets[baseVisibleIndex + writtenSegmentCount] =
                visibleMeshlet;
            ++writtenSegmentCount;
        }
    }
}
