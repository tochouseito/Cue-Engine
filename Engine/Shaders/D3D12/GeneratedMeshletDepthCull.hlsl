// Build a visible meshlet list for the generated depth stream.

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

struct VisibleMeshlet
{
    uint objectIndex;
    uint meshId;
    uint meshletIndex;
    uint padding;
};

cbuffer DispatchParam : register(b0)
{
    uint g_objectCount;
};

cbuffer MaxVisibleMeshletParam : register(b1)
{
    uint g_maxVisibleMeshletCount;
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

RWStructuredBuffer<VisibleMeshlet> g_visibleMeshlets : register(u0);
RWByteAddressBuffer g_counters : register(u1);

static const uint kVisibleMeshletCounterOffset = 0u;

bool reserve_visible_meshlet(out uint visibleIndex)
{
    visibleIndex = 0u;
    [loop]
    for (;;)
    {
        const uint currentCount = g_counters.Load(kVisibleMeshletCounterOffset);
        if (currentCount >= g_maxVisibleMeshletCount)
        {
            return false;
        }

        uint previousCount = 0u;
        g_counters.InterlockedCompareExchange(
            kVisibleMeshletCounterOffset, currentCount, currentCount + 1u,
            previousCount);
        if (previousCount == currentCount)
        {
            visibleIndex = currentCount;
            return true;
        }
    }
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

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint objectIndex = dispatchThreadId.x;
    const uint visibleObjectCount = g_renderObjectCount.Load(0);
    if (objectIndex >= visibleObjectCount || objectIndex >= g_objectCount)
    {
        return;
    }

    const RenderObject renderObject = g_renderObjects[objectIndex];
    if ((renderObject.drawFlags & 1u) != 0u)
    {
        return;
    }

    const MeshRange meshRange = g_meshRanges[renderObject.meshId];
    if (meshRange.meshletCount == 0u || meshRange.rangeIndexCount == 0u)
    {
        return;
    }

    const Transform transform = g_transforms[renderObject.transformId];
    const float radiusScale = transform_radius_scale(transform.worldMatrix);

    [loop]
    for (uint meshletOffset = 0u; meshletOffset < meshRange.meshletCount;
         ++meshletOffset)
    {
        const uint meshletIndex = meshRange.firstMeshlet + meshletOffset;
        const MeshletBounds bounds = g_meshletBounds[meshletIndex];
        if (bounds.indexCount == 0u)
        {
            continue;
        }

        const float4 worldCenter =
            mul(float4(bounds.center, 1.0f), transform.worldMatrix);
        const float4 viewCenter = mul(worldCenter, g_viewMatrix);
        if (!is_view_sphere_inside_frustum(
                viewCenter.xyz, bounds.radius * radiusScale))
        {
            continue;
        }

        uint visibleIndex = 0u;
        if (!reserve_visible_meshlet(visibleIndex))
        {
            return;
        }

        VisibleMeshlet visibleMeshlet;
        visibleMeshlet.objectIndex = objectIndex;
        visibleMeshlet.meshId = renderObject.meshId;
        visibleMeshlet.meshletIndex = meshletIndex;
        visibleMeshlet.padding = 0u;
        g_visibleMeshlets[visibleIndex] = visibleMeshlet;
    }
}
