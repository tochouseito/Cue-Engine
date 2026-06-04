struct RenderCell
{
    float4 boundsCenterRadius;
    uint objectStart;
    uint objectCount;
    uint lodBias;
    uint flags;
};

cbuffer CellCountParam : register(b0)
{
    uint g_cellCount;
};

cbuffer TileCountXParam : register(b1)
{
    uint g_tileCountX;
};

cbuffer TileCountYParam : register(b2)
{
    uint g_tileCountY;
};

cbuffer TileSizeParam : register(b3)
{
    uint g_tileSize;
};

cbuffer ViewProjection : register(b4)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
};

StructuredBuffer<RenderCell> g_cells : register(t0);
ByteAddressBuffer g_hizDepth : register(t1);
RWStructuredBuffer<uint> g_visibleCellIndices : register(u0);
RWByteAddressBuffer g_visibleCellCount : register(u1);

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

bool project_bounds_to_hiz_tiles(
    float4 boundsCenterRadius,
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

    const float radius = boundsCenterRadius.w;
    const float4 viewCenter =
        mul(float4(boundsCenterRadius.xyz, 1.0f), g_viewMatrix);
    const float viewZ = viewCenter.z;
    const float4 clipCenter = mul(viewCenter, g_projectionMatrix);
    if (radius <= 0.0f || abs(clipCenter.w) <= 0.000001f)
    {
        return false;
    }

    const float projection00 = max(abs(g_projectionMatrix[0][0]), 0.000001f);
    const float projection11 = max(abs(g_projectionMatrix[1][1]), 0.000001f);
    const float2 ndcCenter = clipCenter.xy / clipCenter.w;
    const float projectedRadiusX = radius * projection00 / max(viewZ, 0.000001f);
    const float projectedRadiusY = radius * projection11 / max(viewZ, 0.000001f);

    const float2 minNdc =
        max(ndcCenter - float2(projectedRadiusX, projectedRadiusY), float2(-1.0f, -1.0f));
    const float2 maxNdc =
        min(ndcCenter + float2(projectedRadiusX, projectedRadiusY), float2(1.0f, 1.0f));
    const float2 screenSize =
        float2((float)(g_tileCountX * g_tileSize), (float)(g_tileCountY * g_tileSize));
    const float2 minPixel = (minNdc * float2(0.5f, -0.5f) + 0.5f) * screenSize;
    const float2 maxPixel = (maxNdc * float2(0.5f, -0.5f) + 0.5f) * screenSize;
    const float2 rectMin = min(minPixel, maxPixel);
    const float2 rectMax = max(minPixel, maxPixel);

    minTile = min((uint2)floor(rectMin / (float)g_tileSize), uint2(g_tileCountX - 1u, g_tileCountY - 1u));
    maxTile = min((uint2)floor(rectMax / (float)g_tileSize), uint2(g_tileCountX - 1u, g_tileCountY - 1u));
    nearDepth = quantize_depth(project_device_depth(max(viewZ - radius, 0.001f)));
    return true;
}

bool is_occluded_by_hiz(float4 boundsCenterRadius)
{
    uint2 minTile;
    uint2 maxTile;
    uint nearDepth;
    if (!project_bounds_to_hiz_tiles(boundsCenterRadius, minTile, maxTile, nearDepth))
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

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint cellIndex = dispatchThreadId.x;
    bool visible = cellIndex < g_cellCount;
    RenderCell cell;
    if (visible)
    {
        cell = g_cells[cellIndex];
        visible =
            cell.flags != 0u &&
            is_sphere_inside_frustum(cell.boundsCenterRadius) &&
            !is_occluded_by_hiz(cell.boundsCenterRadius);
    }

    const uint waveVisibleCount = WaveActiveCountBits(visible);
    if (waveVisibleCount == 0u)
    {
        return;
    }

    uint waveBaseOffset = 0u;
    if (WaveIsFirstLane())
    {
        g_visibleCellCount.InterlockedAdd(0, waveVisibleCount, waveBaseOffset);
    }
    waveBaseOffset = WaveReadLaneFirst(waveBaseOffset);

    if (!visible)
    {
        return;
    }

    const uint visibleCellOffset =
        waveBaseOffset + WavePrefixCountBits(visible);
    if (visibleCellOffset < g_cellCount)
    {
        g_visibleCellIndices[visibleCellOffset] = cellIndex;
    }
}
