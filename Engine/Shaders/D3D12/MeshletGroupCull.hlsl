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

RWStructuredBuffer<uint> g_objectDrawModes : register(u0);
RWStructuredBuffer<IndirectCommand> g_rangeCommands : register(u1);
RWByteAddressBuffer g_rangeCommandCount : register(u2);

static const uint kDrawModeNormal = 0u;
static const uint kDrawModeGroupRange = 1u;
static const uint kDrawModeCulled = 2u;
static const uint kDrawModeFallback = 3u;
static const uint kMeshletsPerGroup = 8u;
static const uint kMaxRangesPerObject = 6u;
static const uint kMaxMergeGapIndices = 96u;

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

void build_group_bounds(
    uint firstMeshlet,
    uint meshletCount,
    uint groupIndex,
    out float3 center,
    out float radius,
    out uint firstIndex,
    out uint indexCount)
{
    const uint groupFirst = groupIndex * kMeshletsPerGroup;
    const uint groupCount = min(kMeshletsPerGroup, meshletCount - groupFirst);

    center = float3(0.0f, 0.0f, 0.0f);
    firstIndex = 0xffffffffu;
    uint endIndex = 0u;

    [loop]
    for (uint i = 0u; i < groupCount; ++i)
    {
        const MeshletBounds bounds =
            g_meshletBounds[firstMeshlet + groupFirst + i];
        center += bounds.center;
        firstIndex = min(firstIndex, bounds.firstIndex);
        endIndex = max(endIndex, bounds.firstIndex + bounds.indexCount);
    }

    center /= max((float)groupCount, 1.0f);
    radius = 0.0f;
    [loop]
    for (uint j = 0u; j < groupCount; ++j)
    {
        const MeshletBounds bounds =
            g_meshletBounds[firstMeshlet + groupFirst + j];
        radius = max(radius, length(bounds.center - center) + bounds.radius);
    }

    indexCount = firstIndex < endIndex ? endIndex - firstIndex : 0u;
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
    uint commandBase = 0u;
    g_rangeCommandCount.InterlockedAdd(0, rangeCount, commandBase);
    if (commandBase + rangeCount > g_maxRangeCommandCount)
    {
        return false;
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
    if (objectIndex >= visibleObjectCount || objectIndex >= g_objectCount)
    {
        return;
    }

    g_objectDrawModes[objectIndex] = kDrawModeNormal;

    const RenderObject renderObject = g_renderObjects[objectIndex];
    const MeshRange meshRange = g_meshRanges[renderObject.meshId];
    if ((renderObject.drawFlags & 1u) != 0u ||
        meshRange.meshletCount < (kMeshletsPerGroup * 2u) ||
        meshRange.rangeIndexCount == 0u)
    {
        return;
    }

    // Very small objects are cheaper as batched whole-mesh draws.
    if (projected_radius(renderObject.boundsCenterRadius) < 0.08f)
    {
        return;
    }

    const Transform transform = g_transforms[renderObject.transformId];
    const float radiusScale = transform_radius_scale(transform.worldMatrix);
    const uint groupCount =
        (meshRange.meshletCount + kMeshletsPerGroup - 1u) / kMeshletsPerGroup;

    uint rangeStarts[kMaxRangesPerObject];
    uint rangeEnds[kMaxRangesPerObject];
    uint rangeCount = 0u;
    uint visibleGroupCount = 0u;
    uint visibleIndexCount = 0u;
    uint rangeDrawnIndexCount = 0u;
    bool rangeOverflow = false;

    [loop]
    for (uint groupIndex = 0u; groupIndex < groupCount; ++groupIndex)
    {
        float3 localCenter;
        float localRadius;
        uint groupFirstIndex;
        uint groupIndexCount;
        build_group_bounds(meshRange.firstMeshlet, meshRange.meshletCount,
                           groupIndex, localCenter, localRadius,
                           groupFirstIndex, groupIndexCount);
        if (groupIndexCount == 0u)
        {
            continue;
        }

        const float4 worldCenter =
            mul(float4(localCenter, 1.0f), transform.worldMatrix);
        const float4 viewCenter = mul(worldCenter, g_viewMatrix);
        const float worldRadius = localRadius * radiusScale;
        if (!is_view_sphere_inside_frustum(viewCenter.xyz, worldRadius))
        {
            continue;
        }

        ++visibleGroupCount;
        visibleIndexCount += groupIndexCount;
        append_or_merge_range(groupFirstIndex,
                              groupFirstIndex + groupIndexCount,
                              rangeCount, rangeStarts, rangeEnds,
                              rangeOverflow);
    }

    if (visibleGroupCount == 0u)
    {
        g_objectDrawModes[objectIndex] = kDrawModeCulled;
        return;
    }

    if (rangeOverflow || rangeCount == 0u)
    {
        g_objectDrawModes[objectIndex] = kDrawModeFallback;
        return;
    }

    [loop]
    for (uint rangeIndex = 0u; rangeIndex < rangeCount; ++rangeIndex)
    {
        rangeDrawnIndexCount += rangeEnds[rangeIndex] - rangeStarts[rangeIndex];
    }

    const bool almostWholeMesh =
        rangeDrawnIndexCount * 100u >= meshRange.rangeIndexCount * 85u;
    const bool tooManyRanges = rangeCount > 4u;
    const bool weakSavings =
        visibleIndexCount * 100u >= meshRange.rangeIndexCount * 70u &&
        rangeCount > 1u;
    if (almostWholeMesh || tooManyRanges || weakSavings)
    {
        g_objectDrawModes[objectIndex] = kDrawModeFallback;
        return;
    }

    if (emit_range_commands(objectIndex, meshRange.baseVertex, rangeCount,
                            rangeStarts, rangeEnds))
    {
        g_objectDrawModes[objectIndex] = kDrawModeGroupRange;
    }
    else
    {
        g_objectDrawModes[objectIndex] = kDrawModeFallback;
    }
}
