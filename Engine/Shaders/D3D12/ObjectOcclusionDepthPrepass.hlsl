// Legacy object-bounds depth prepass.
// Each object bounds is projected into tiles and writes nearest depth, producing
// coarse occlusion depth similar to Hi-Z.

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

cbuffer DispatchParam : register(b0)
{
    uint g_objectCount;
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

StructuredBuffer<RenderableInfo> g_renderableInfos : register(t0);
RWByteAddressBuffer g_occlusionDepth : register(u0);

uint quantize_depth(float viewDepth)
{
    return (uint)min(max(viewDepth, 0.0f) * 10000.0f, 4294967294.0f);
}

bool project_bounds(float4 boundsCenterRadius, out uint2 minTile, out uint2 maxTile, out uint depth)
{
    minTile = uint2(0u, 0u);
    maxTile = uint2(0u, 0u);
    depth = 0xffffffffu;

    const float radius = boundsCenterRadius.w;
    if (radius <= 0.0f)
    {
        return false;
    }

    const float4 viewCenter =
        mul(float4(boundsCenterRadius.xyz, 1.0f), g_viewMatrix);
    const float viewZ = viewCenter.z;

    const float projection00 = max(abs(g_projectionMatrix[0][0]), 0.000001f);
    const float projection11 = max(abs(g_projectionMatrix[1][1]), 0.000001f);
    const float tanHalfFovX = 1.0f / projection00;
    const float tanHalfFovY = 1.0f / projection11;

    const float projection22 = g_projectionMatrix[2][2];
    const float projection32 = g_projectionMatrix[3][2];
    const float nearClip = -projection32 / max(projection22 + 1.0f, 0.000001f);
    const float farClip = projection32 / min(1.0f - projection22, -0.000001f);

    if (viewZ + radius < nearClip || viewZ - radius > farClip)
    {
        return false;
    }

    const float horizontalLimit = abs(viewZ) * tanHalfFovX + radius;
    if (abs(viewCenter.x) > horizontalLimit)
    {
        return false;
    }

    const float verticalLimit = abs(viewZ) * tanHalfFovY + radius;
    if (abs(viewCenter.y) > verticalLimit)
    {
        return false;
    }

    const float4 clipCenter = mul(viewCenter, g_projectionMatrix);
    if (abs(clipCenter.w) <= 0.000001f)
    {
        return false;
    }

    const float2 ndcCenter = clipCenter.xy / clipCenter.w;
    const float projectedRadiusX = radius * projection00 / max(viewZ, 0.000001f);
    const float projectedRadiusY = radius * projection11 / max(viewZ, 0.000001f);

    const float2 minNdc = max(ndcCenter - float2(projectedRadiusX, projectedRadiusY), float2(-1.0f, -1.0f));
    const float2 maxNdc = min(ndcCenter + float2(projectedRadiusX, projectedRadiusY), float2(1.0f, 1.0f));
    const float2 screenSize = float2(
        (float)(g_tileCountX * g_tileSize),
        (float)(g_tileCountY * g_tileSize));
    const float2 minPixel = (minNdc * float2(0.5f, -0.5f) + 0.5f) * screenSize;
    const float2 maxPixel = (maxNdc * float2(0.5f, -0.5f) + 0.5f) * screenSize;

    const float2 rectMin = min(minPixel, maxPixel);
    const float2 rectMax = max(minPixel, maxPixel);
    minTile = min((uint2)floor(rectMin / (float)g_tileSize), uint2(g_tileCountX - 1u, g_tileCountY - 1u));
    maxTile = min((uint2)floor(rectMax / (float)g_tileSize), uint2(g_tileCountX - 1u, g_tileCountY - 1u));
    depth = quantize_depth(max(viewZ - radius, nearClip));
    return true;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint objectId = dispatchThreadId.x;
    if (objectId >= g_objectCount)
    {
        return;
    }

    const RenderableInfo renderableInfo = g_renderableInfos[objectId];
    if (renderableInfo.visible == 0u)
    {
        return;
    }

    uint2 minTile;
    uint2 maxTile;
    uint depth;
    if (!project_bounds(renderableInfo.boundsCenterRadius, minTile, maxTile, depth))
    {
        return;
    }

    // Atomic min writes the nearest bounds depth into covered tiles.
    // Bounds depth is conservative compared with raster depth.
    for (uint tileY = minTile.y; tileY <= maxTile.y; ++tileY)
    {
        for (uint tileX = minTile.x; tileX <= maxTile.x; ++tileX)
        {
            const uint tileIndex = tileY * g_tileCountX + tileX;
            uint previousDepth = 0u;
            g_occlusionDepth.InterlockedMin(tileIndex * 4u, depth, previousDepth);
        }
    }
}
