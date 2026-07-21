// Build a coarse visible chunk worklist for MeshShaderVisibility.

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

struct CandidateChunk
{
    uint objectIndex;
    uint meshId;
    uint firstMeshlet;
    uint meshletCountAndSegmentCount;
};

cbuffer MaxObjectParam : register(b0)
{
    uint g_maxObjectCount;
};

cbuffer MaxCandidateChunkParam : register(b1)
{
    uint g_maxCandidateChunkCount;
};

cbuffer ViewProjection : register(b2)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
};

cbuffer MaxMeshParam : register(b3)
{
    uint g_maxMeshCount;
};

cbuffer MaxMeshletChunkParam : register(b4)
{
    uint g_maxMeshletChunkCount;
};

cbuffer ChunkCullFlagsParam : register(b5)
{
    uint g_enableChunkFrustumCull;
};

cbuffer AdvancedLodFlagsParam : register(b6)
{
    uint g_enableAdvancedLod;
};

cbuffer RenderHeightParam : register(b7)
{
    uint g_renderHeight;
};

cbuffer Lod1ScreenRadiusParam : register(b8)
{
    float g_lod1ScreenRadiusPx;
};

cbuffer Lod2ScreenRadiusParam : register(b9)
{
    float g_lod2ScreenRadiusPx;
};

cbuffer HiZWidthParam : register(b10)
{
    uint g_hizWidth;
};

cbuffer HiZHeightParam : register(b11)
{
    uint g_hizHeight;
};

cbuffer OcclusionParam : register(b12)
{
    uint g_occlusionEnabled;
};

cbuffer HybridMinMeshletParam : register(b13)
{
    uint g_hybridMinMeshletCount;
};

cbuffer HybridMinScreenRadiusParam : register(b14)
{
    float g_hybridMinScreenRadiusPx;
};

StructuredBuffer<RenderObject> g_renderObjects : register(t0);
StructuredBuffer<Transform> g_transforms : register(t1);
ByteAddressBuffer g_visibleObjectCount : register(t2);
StructuredBuffer<MeshChunkRange> g_meshChunkRanges : register(t3);
StructuredBuffer<MeshletChunk> g_meshletChunks : register(t4);
StructuredBuffer<MeshRange> g_meshRanges : register(t5);
Texture2D<uint> g_occlusionHiZ : register(t6);

RWStructuredBuffer<CandidateChunk> g_candidateChunks : register(u0);
RWByteAddressBuffer g_counters : register(u1);

static const uint kCandidateChunkCounterOffset = 4u;
static const uint kMaxMeshletsPerChunk = 8u;
static const uint kMaxMeshletSegmentsPerChunk = 16u;
static const uint kRenderObjectFlagForwardFallback = 1u << 0u;
static const uint kDrawFlagImpostor = 1u << 0u;
static const uint kVisibilityPrimitiveBits = 19u;
static const uint kMaxPackedVisibilityIndexCount =
    (1u << kVisibilityPrimitiveBits) * 3u;
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

bool is_sphere_fully_inside_plane(float4 plane, float3 center, float radius)
{
    const float invPlaneLength =
        rsqrt(max(dot(plane.xyz, plane.xyz), 0.000000000001f));
    const float signedDistance =
        (dot(plane.xyz, center) + plane.w) * invPlaneLength;
    return signedDistance >= radius;
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

bool is_view_sphere_fully_inside_frustum(float3 viewCenter, float radius)
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

    return
        is_sphere_fully_inside_plane(
            projectionColumn3 + projectionColumn0, viewCenter, radius) &&
        is_sphere_fully_inside_plane(
            projectionColumn3 - projectionColumn0, viewCenter, radius) &&
        is_sphere_fully_inside_plane(
            projectionColumn3 + projectionColumn1, viewCenter, radius) &&
        is_sphere_fully_inside_plane(
            projectionColumn3 - projectionColumn1, viewCenter, radius) &&
        is_sphere_fully_inside_plane(
            projectionColumn3 + projectionColumn2, viewCenter, radius) &&
        is_sphere_fully_inside_plane(
            projectionColumn3 - projectionColumn2, viewCenter, radius);
}

float transform_radius_scale(float4x4 worldMatrix)
{
    return max(length(worldMatrix[0].xyz),
               max(length(worldMatrix[1].xyz), length(worldMatrix[2].xyz)));
}

float projected_screen_radius_px(float radius, float viewZ)
{
    const float projectionScale = abs(g_projectionMatrix[1][1]);
    return radius * projectionScale * (float)g_renderHeight /
           max(abs(viewZ), 0.001f);
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

bool is_chunk_occluded_by_hiz(float3 viewCenter, float radius)
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
    const uint2 samples[5] =
    {
        center,
        uint2((minX + center.x) >> 1u, center.y),
        uint2((maxX + center.x + 1u) >> 1u, center.y),
        uint2(center.x, (minY + center.y) >> 1u),
        uint2(center.x, (maxY + center.y + 1u) >> 1u)
    };

    const float chunkNearDepth =
        project_device_depth(max(viewZ - radius, 0.001f));
    [unroll]
    for (uint sampleIndex = 0u; sampleIndex < 5u; ++sampleIndex)
    {
        const float occluderDepth =
            decode_hiz_depth(g_occlusionHiZ.Load(int3(samples[sampleIndex], 0)));
        if (chunkNearDepth <= occluderDepth + kOcclusionDepthBias)
        {
            return false;
        }
    }

    return true;
}

uint select_desired_lod(float screenRadiusPx)
{
    if (g_enableAdvancedLod == 0u)
    {
        return 0u;
    }
    if (screenRadiusPx < g_lod2ScreenRadiusPx)
    {
        return 2u;
    }
    if (screenRadiusPx < g_lod1ScreenRadiusPx)
    {
        return 1u;
    }
    return 0u;
}

bool reserve_candidate_chunk(out uint chunkIndex)
{
    chunkIndex = 0u;
    uint previousCount = 0u;
    g_counters.InterlockedAdd(kCandidateChunkCounterOffset, 1u,
                              previousCount);
    if (previousCount >= g_maxCandidateChunkCount)
    {
        return false;
    }

    chunkIndex = previousCount;
    return true;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint objectIndex = dispatchThreadId.x;
    const uint visibleObjectCount = g_visibleObjectCount.Load(0);
    if (objectIndex >= visibleObjectCount || objectIndex >= g_maxObjectCount)
    {
        return;
    }

    const RenderObject renderObject = g_renderObjects[objectIndex];
    if ((renderObject.drawFlags & 1u) != 0u)
    {
        return;
    }
    if (renderObject.meshId >= g_maxMeshCount)
    {
        return;
    }
    if (renderObject.transformId >= g_maxObjectCount)
    {
        return;
    }

    const MeshRange meshRange = g_meshRanges[renderObject.meshId];
    if ((renderObject.padding & kRenderObjectFlagForwardFallback) != 0u ||
        (renderObject.drawFlags & kDrawFlagImpostor) != 0u ||
        meshRange.indexCount == 0u ||
        meshRange.indexCount > kMaxPackedVisibilityIndexCount)
    {
        return;
    }

    const float4 objectViewCenter = mul(
        float4(renderObject.boundsCenterRadius.xyz, 1.0f), g_viewMatrix);
    const float objectRadius = renderObject.boundsCenterRadius.w;
    const float objectScreenRadiusPx =
        projected_screen_radius_px(objectRadius, objectViewCenter.z);
    if (meshRange.meshletCount < g_hybridMinMeshletCount ||
        objectScreenRadiusPx < g_hybridMinScreenRadiusPx ||
        is_view_sphere_fully_inside_frustum(
            objectViewCenter.xyz, objectRadius * 1.05f))
    {
        return;
    }

    const MeshChunkRange chunkRange = g_meshChunkRanges[renderObject.meshId];
    if (chunkRange.chunkCount == 0u ||
        chunkRange.firstChunk >= g_maxMeshletChunkCount)
    {
        return;
    }

    const Transform transform = g_transforms[renderObject.transformId];
    const float radiusScale = transform_radius_scale(transform.worldMatrix);
    const uint boundedChunkCount =
        min(chunkRange.chunkCount, g_maxMeshletChunkCount - chunkRange.firstChunk);
    [loop]
    for (uint localChunkIndex = 0u; localChunkIndex < boundedChunkCount;
         ++localChunkIndex)
    {
        const uint globalChunkIndex = chunkRange.firstChunk + localChunkIndex;
        const MeshletChunk chunk =
            g_meshletChunks[globalChunkIndex];
        if (chunk.indexCount == 0u || chunk.meshletCount == 0u)
        {
            continue;
        }

        const float4 worldCenter =
            mul(float4(chunk.boundsCenter, 1.0f), transform.worldMatrix);
        const float4 viewCenter = mul(worldCenter, g_viewMatrix);
        const float chunkRadius = chunk.boundsRadius * radiusScale;
        if (g_enableChunkFrustumCull != 0u &&
            !is_view_sphere_inside_frustum(
                viewCenter.xyz, chunkRadius * 1.10f))
        {
            continue;
        }
        if (is_chunk_occluded_by_hiz(viewCenter.xyz, chunkRadius))
        {
            continue;
        }
        const uint desiredLod =
            select_desired_lod(projected_screen_radius_px(chunkRadius,
                                                          viewCenter.z));
        if (chunk.lod != 0u && chunk.lod != desiredLod)
        {
            continue;
        }

        const uint candidateMeshletCount =
            min(chunk.meshletCount, kMaxMeshletsPerChunk);
        const uint segmentCount =
            min(candidateMeshletCount * 2u, kMaxMeshletSegmentsPerChunk);
        if (segmentCount == 0u)
        {
            continue;
        }

        uint candidateIndex = 0u;
        if (!reserve_candidate_chunk(candidateIndex))
        {
            return;
        }

        CandidateChunk candidate;
        candidate.objectIndex = objectIndex;
        candidate.meshId = renderObject.meshId;
        candidate.firstMeshlet = chunk.firstMeshlet;
        candidate.meshletCountAndSegmentCount =
            candidateMeshletCount | (segmentCount << 16u);
        g_candidateChunks[candidateIndex] = candidate;
    }
}
