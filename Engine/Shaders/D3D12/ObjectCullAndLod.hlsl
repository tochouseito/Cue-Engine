// Final object culling / LOD selection pass.
// This path keeps only object visibility, frustum culling, distance/screen-size
// LOD, and screen-edge LOD bias before static mesh batching.

struct RenderableInfo
{
    uint objectId;
    uint visible;
    uint meshId;
    uint transformId;
    uint materialId;
    uint castsShadow;
    uint receivesShadow;
    uint shadowCasterMode;
    uint skinPaletteOffset;
    uint skinPaletteCount;
    uint lodMeshId0;
    uint lodMeshId1;
    uint lodMeshId2;
    uint lodMeshId3;
    uint lodMeshId4;
    uint lodCount;
    uint occluderMeshId;
    uint occluderFlags;
    uint padding0;
    uint padding1;
    float4 boundsCenterRadius;
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

cbuffer DispatchParam : register(b0)
{
    uint g_objectCount;
};

cbuffer ViewProjection : register(b1)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
};

StructuredBuffer<RenderableInfo> g_renderableInfos : register(t0);
Texture2D<uint> g_occlusionHiZ : register(t1);
RWStructuredBuffer<RenderObject> g_renderObjects : register(u0);
RWByteAddressBuffer g_renderObjectCount : register(u1);

cbuffer HiZWidthParam : register(b2)
{
    uint g_hizWidth;
};

cbuffer HiZHeightParam : register(b3)
{
    uint g_hizHeight;
};

cbuffer OcclusionEnabledParam : register(b4)
{
    uint g_occlusionEnabled;
};

RWByteAddressBuffer g_occlusionStats : register(u2);

static const uint kOcclusionStatsEnabled = 84u;
static const uint kOcclusionStatsTested = 88u;
static const uint kOcclusionStatsRejected = 92u;

bool is_sphere_inside_plane(float4 plane, float3 center, float radius)
{
    const float invPlaneLength =
        rsqrt(max(dot(plane.xyz, plane.xyz), 0.000000000001f));
    const float signedDistance =
        (dot(plane.xyz, center) + plane.w) * invPlaneLength;
    return signedDistance >= -radius;
}

bool is_sphere_inside_frustum(float4 boundsCenterRadius)
{
    const float radius = boundsCenterRadius.w;
    if (radius <= 0.0f)
    {
        return false;
    }

    const float4 viewCenter =
        mul(float4(boundsCenterRadius.xyz, 1.0f), g_viewMatrix);

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

uint get_lod_mesh_id(RenderableInfo renderableInfo, uint lodIndex)
{
    if (lodIndex == 1u)
    {
        return renderableInfo.lodMeshId1;
    }
    if (lodIndex == 2u)
    {
        return renderableInfo.lodMeshId2;
    }
    if (lodIndex == 3u)
    {
        return renderableInfo.lodMeshId3;
    }
    if (lodIndex == 4u)
    {
        return renderableInfo.lodMeshId4;
    }
    return renderableInfo.lodMeshId0;
}

uint select_view_center_lod_bias(float4 viewCenter, float projectedRadius)
{
    const float4 clipCenter = mul(viewCenter, g_projectionMatrix);
    if (abs(clipCenter.w) <= 0.000001f)
    {
        return 0u;
    }

    const float2 ndcCenter = clipCenter.xy / clipCenter.w;
    const float centerDistance = length(ndcCenter);

    uint bias = 0u;
    if (centerDistance >= 0.9f)
    {
        bias = 2u;
    }
    else if (centerDistance >= 0.6f)
    {
        bias = 1u;
    }

    if (projectedRadius >= 0.35f)
    {
        bias = min(bias, 1u);
    }
    return bias;
}

uint select_lod(RenderableInfo renderableInfo)
{
    const uint lodCount = max(renderableInfo.lodCount, 1u);
    if (lodCount <= 1u)
    {
        return 0u;
    }

    const float4 viewCenter =
        mul(float4(renderableInfo.boundsCenterRadius.xyz, 1.0f), g_viewMatrix);
    const float viewZ = max(viewCenter.z, 0.001f);
    const float projectedRadius =
        renderableInfo.boundsCenterRadius.w *
        abs(g_projectionMatrix[1][1]) /
        viewZ;

    uint lodIndex = 0u;
    if (projectedRadius < 0.012f)
    {
        lodIndex = 4u;
    }
    else if (projectedRadius < 0.08f)
    {
        lodIndex = 3u;
    }
    else if (projectedRadius < 0.18f)
    {
        lodIndex = 2u;
    }
    else if (projectedRadius < 0.35f)
    {
        lodIndex = 1u;
    }

    const uint viewCenterBias =
        select_view_center_lod_bias(viewCenter, projectedRadius);
    if (viewCenterBias == 0u || lodIndex >= 2u)
    {
        return min(lodIndex, lodCount - 1u);
    }
    return min(lodIndex + viewCenterBias, min(2u, lodCount - 1u));
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

uint select_depth_bin(float4 boundsCenterRadius)
{
    const float4 viewCenter =
        mul(float4(boundsCenterRadius.xyz, 1.0f), g_viewMatrix);
    const float deviceDepth = project_device_depth(max(viewCenter.z, 0.001f));
    return min((uint)floor(saturate(deviceDepth) * 8.0f), 7u);
}

float decode_hiz_depth(uint depth)
{
    return (float)depth * (1.0f / 4294967295.0f);
}

bool is_occluded_by_hiz(float4 boundsCenterRadius)
{
    if (g_occlusionEnabled == 0u || g_hizWidth == 0u || g_hizHeight == 0u)
    {
        return false;
    }

    const float4 viewCenter =
        mul(float4(boundsCenterRadius.xyz, 1.0f), g_viewMatrix);
    const float viewZ = viewCenter.z;
    const float radius = boundsCenterRadius.w;
    if (viewZ <= radius + 0.001f || radius <= 0.0f)
    {
        return false;
    }

    const float4 clipCenter = mul(viewCenter, g_projectionMatrix);
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
    const uint2 center = uint2((minX + maxX) >> 1u, (minY + maxY) >> 1u);

    const uint rectWidth = maxX - minX + 1u;
    const uint rectHeight = maxY - minY + 1u;
    if (rectWidth > 8u || rectHeight > 8u)
    {
        return false;
    }

    const float objectNearDepth = project_device_depth(max(viewZ - radius, 0.001f));
    const float depthBias = 0.003f;

    const uint2 samples[5] =
    {
        uint2(minX, minY),
        uint2(maxX, minY),
        uint2(minX, maxY),
        uint2(maxX, maxY),
        center
    };

    [unroll]
    for (uint sampleIndex = 0u; sampleIndex < 5u; ++sampleIndex)
    {
        const float occluderMaxDepth =
            decode_hiz_depth(g_occlusionHiZ.Load(int3(samples[sampleIndex], 0)));
        if (objectNearDepth <= occluderMaxDepth + depthBias)
        {
            return false;
        }
    }

    return true;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint objectId = dispatchThreadId.x;
    bool visible = objectId < g_objectCount;
    RenderableInfo renderableInfo;
    if (visible)
    {
        renderableInfo = g_renderableInfos[objectId];
        visible =
            renderableInfo.visible != 0u &&
            is_sphere_inside_frustum(renderableInfo.boundsCenterRadius);
        if (visible && g_occlusionEnabled != 0u)
        {
            g_occlusionStats.Store(kOcclusionStatsEnabled, 1u);
            g_occlusionStats.InterlockedAdd(kOcclusionStatsTested, 1u);
            if (is_occluded_by_hiz(renderableInfo.boundsCenterRadius))
            {
                g_occlusionStats.InterlockedAdd(kOcclusionStatsRejected, 1u);
                visible = false;
            }
        }
    }

    const uint waveVisibleCount = WaveActiveCountBits(visible);
    if (waveVisibleCount == 0u)
    {
        return;
    }

    uint waveBaseOffset = 0u;
    if (WaveIsFirstLane())
    {
        g_renderObjectCount.InterlockedAdd(
            0, waveVisibleCount, waveBaseOffset);
    }
    waveBaseOffset = WaveReadLaneFirst(waveBaseOffset);

    if (!visible)
    {
        return;
    }

    const uint lodIndex = select_lod(renderableInfo);
    const uint objectOffset = waveBaseOffset + WavePrefixCountBits(visible);
    if (objectOffset >= g_objectCount)
    {
        return;
    }

    RenderObject renderObject;
    renderObject.objectId = renderableInfo.objectId;
    renderObject.meshId = get_lod_mesh_id(renderableInfo, lodIndex);
    renderObject.transformId = renderableInfo.transformId;
    renderObject.materialId = renderableInfo.materialId;
    renderObject.castsShadow = renderableInfo.castsShadow;
    renderObject.receivesShadow = renderableInfo.receivesShadow;
    renderObject.shadowCasterMode = renderableInfo.shadowCasterMode;
    renderObject.skinPaletteOffset = renderableInfo.skinPaletteOffset;
    renderObject.skinPaletteCount = renderableInfo.skinPaletteCount;
    renderObject.drawFlags = lodIndex == 4u ? 1u : 0u;
    renderObject.depthBin = select_depth_bin(renderableInfo.boundsCenterRadius);
    renderObject.padding = renderableInfo.padding0;
    renderObject.boundsCenterRadius = renderableInfo.boundsCenterRadius;
    g_renderObjects[objectOffset] = renderObject;
}
