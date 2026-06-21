// ObjectCulling 後の visible object に対して、meshlet bounds で追加判定する。
// ここでは部分描画せず、全 meshlet が不可視な場合だけ object を後段 batching
// から除外する。

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
    uint padding0;
    uint padding1;
    uint padding2;
};

struct MeshletBounds
{
    float3 center;
    float radius;
    uint firstIndex;
    uint indexCount;
    uint padding0;
    uint padding1;
};

cbuffer ObjectCountParam : register(b0)
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

cbuffer MinMeshletCountParam : register(b4)
{
    uint g_minMeshletCount;
};

cbuffer MinProjectedRadiusParam : register(b5)
{
    float g_minProjectedRadius;
};

cbuffer ViewProjection : register(b6)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
};

StructuredBuffer<RenderObject> g_renderObjects : register(t0);
StructuredBuffer<Transform> g_transforms : register(t1);
ByteAddressBuffer g_renderObjectCount : register(t2);
StructuredBuffer<MeshRange> g_meshRanges : register(t3);
StructuredBuffer<MeshletBounds> g_meshletBounds : register(t4);
ByteAddressBuffer g_hizDepth : register(t5);
RWStructuredBuffer<uint> g_refinedVisibility : register(u0);

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
    nearDepth = quantize_depth(project_device_depth(max(viewZ - radius, 0.001f)));
    return true;
}

bool is_occluded_by_hiz(float3 worldCenter, float radius)
{
    uint2 minTile;
    uint2 maxTile;
    uint nearDepth;
    if (!project_sphere_to_hiz_tiles(worldCenter, radius, minTile, maxTile, nearDepth))
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

float object_projected_radius(RenderObject renderObject)
{
    const float4 viewCenter =
        mul(float4(renderObject.boundsCenterRadius.xyz, 1.0f), g_viewMatrix);
    return renderObject.boundsCenterRadius.w *
        abs(g_projectionMatrix[1][1]) /
        max(viewCenter.z, 0.001f);
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint renderObjectIndex = dispatchThreadId.x;
    const uint visibleObjectCount = g_renderObjectCount.Load(0);
    if (renderObjectIndex >= visibleObjectCount ||
        renderObjectIndex >= g_objectCount)
    {
        return;
    }

    const RenderObject renderObject = g_renderObjects[renderObjectIndex];
    uint refinedVisible = 1u;

    const MeshRange meshRange = g_meshRanges[renderObject.meshId];
    if (meshRange.meshletCount == 0u)
    {
        g_refinedVisibility[renderObjectIndex] = refinedVisible;
        return;
    }

    const bool cullMeshlets =
        (renderObject.drawFlags & 1u) == 0u &&
        meshRange.meshletCount >= g_minMeshletCount &&
        object_projected_radius(renderObject) >= g_minProjectedRadius;

    if (!cullMeshlets)
    {
        g_refinedVisibility[renderObjectIndex] = refinedVisible;
        return;
    }

    refinedVisible = 0u;
    const Transform transform = g_transforms[renderObject.transformId];
    const float maxScale = transform_max_scale(transform);
    const uint meshletEnd = meshRange.firstMeshlet + meshRange.meshletCount;
    for (uint meshletIndex = meshRange.firstMeshlet;
         meshletIndex < meshletEnd;
         ++meshletIndex)
    {
        const MeshletBounds meshlet = g_meshletBounds[meshletIndex];
        if (meshlet.indexCount == 0u)
        {
            continue;
        }

        const float3 worldCenter =
            mul(float4(meshlet.center, 1.0f), transform.worldMatrix).xyz;
        const float worldRadius = meshlet.radius * maxScale;
        if (is_sphere_inside_frustum(worldCenter, worldRadius) &&
            !is_occluded_by_hiz(worldCenter, worldRadius))
        {
            refinedVisible = 1u;
            break;
        }
    }

    g_refinedVisibility[renderObjectIndex] = refinedVisible;
}
