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

cbuffer TileCountXParam : register(b2)
{
    uint g_tileCountX;
};

cbuffer TileCountYParam : register(b3)
{
    uint g_tileCountY;
};

cbuffer TileSizeParam : register(b4)
{
    uint g_tileSize;
};

StructuredBuffer<RenderableInfo> g_renderableInfos : register(t0);
ByteAddressBuffer g_hizDepth : register(t1);
RWStructuredBuffer<RenderObject> g_renderObjects : register(u0);
RWByteAddressBuffer g_renderObjectCount : register(u1);

bool is_sphere_inside_frustum(float4 boundsCenterRadius)
{
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

    return true;
}

uint get_lod_mesh_id(RenderableInfo renderableInfo, uint lodIndex)
{
    if (lodIndex == 1)
    {
        return renderableInfo.lodMeshId1;
    }
    if (lodIndex == 2)
    {
        return renderableInfo.lodMeshId2;
    }
    if (lodIndex == 3)
    {
        return renderableInfo.lodMeshId3;
    }
    if (lodIndex == 4)
    {
        return renderableInfo.lodMeshId4;
    }
    return renderableInfo.lodMeshId0;
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

    return min(lodIndex, lodCount - 1u);
}

uint quantize_depth(float depth)
{
    return (uint)min(max(depth, 0.0f) * 4294967295.0f, 4294967295.0f);
}

float project_device_depth(float viewZ)
{
    const float4 clipPosition = mul(float4(0.0f, 0.0f, viewZ, 1.0f), g_projectionMatrix);
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
    const float2 screenSize = float2(
        (float)(g_tileCountX * g_tileSize),
        (float)(g_tileCountY * g_tileSize));
    const float2 minPixel =
        (minNdc * float2(0.5f, -0.5f) + 0.5f) * screenSize;
    const float2 maxPixel =
        (maxNdc * float2(0.5f, -0.5f) + 0.5f) * screenSize;
    const float2 rectMin = min(minPixel, maxPixel);
    const float2 rectMax = max(minPixel, maxPixel);

    minTile = min(
        (uint2)floor(rectMin / (float)g_tileSize),
        uint2(g_tileCountX - 1u, g_tileCountY - 1u));
    maxTile = min(
        (uint2)floor(rectMax / (float)g_tileSize),
        uint2(g_tileCountX - 1u, g_tileCountY - 1u));
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
    const uint objectId = dispatchThreadId.x;

    RenderableInfo renderableInfo;
    bool visible = objectId < g_objectCount;
    if (visible)
    {
        renderableInfo = g_renderableInfos[objectId];
        visible =
            renderableInfo.visible != 0 &&
            is_sphere_inside_frustum(renderableInfo.boundsCenterRadius) &&
            !is_occluded_by_hiz(renderableInfo.boundsCenterRadius);
    }

    if (!visible)
    {
        return;
    }

    const uint lodIndex = select_lod(renderableInfo);
    uint objectOffset = 0;
    g_renderObjectCount.InterlockedAdd(0, 1u, objectOffset);
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
    renderObject.padding = 0u;
    renderObject.boundsCenterRadius = renderableInfo.boundsCenterRadius;
    g_renderObjects[objectOffset] = renderObject;
}
