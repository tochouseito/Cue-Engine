// Amplification-shader variant for mesh shader visibility.

#define CUE_MESH_SHADER_VISIBILITY_EXTERNAL_VISIBLE_MESHLETS 1
#define CUE_MESH_SHADER_VISIBILITY_NO_DIRECT_MS 1
#include "MeshShaderVisibility.hlsl"

struct MeshShaderVisibilityPayload
{
    uint visibleMeshletBaseIndex;
    uint visibleMaskLo;
    uint visibleMaskHi;
};

ByteAddressBuffer g_meshShaderVisibilityCounters : register(t9);
Texture2D<uint> g_asOcclusionHiZ : register(t10);

cbuffer AsHiZWidthParam : register(b1)
{
    uint g_asHiZWidth;
};

cbuffer AsHiZHeightParam : register(b2)
{
    uint g_asHiZHeight;
};

cbuffer AsHiZFlagsParam : register(b3)
{
    uint g_enableAsHiZ;
};

static const uint kVisibleMeshletCounterOffset = 0u;
static const uint kMeshletsPerAmplificationGroup = 64u;
static const float kAsHiZDepthBias = 0.00075f;
static const uint kAsHiZMaxRectTexels = 10u;

groupshared uint g_asVisibleMaskLo;
groupshared uint g_asVisibleMaskHi;

float decode_as_hiz_depth(uint encodedDepth)
{
    return (float)encodedDepth * (1.0f / 4294967295.0f);
}

float transform_radius_scale_as(float4x4 worldMatrix)
{
    return max(length(worldMatrix[0].xyz),
               max(length(worldMatrix[1].xyz), length(worldMatrix[2].xyz)));
}

bool is_payload_meshlet_visible(MeshShaderVisibilityPayload payload,
                                uint localMeshletIndex)
{
    if (localMeshletIndex < 32u)
    {
        return (payload.visibleMaskLo & (1u << localMeshletIndex)) != 0u;
    }
    return (payload.visibleMaskHi & (1u << (localMeshletIndex - 32u))) != 0u;
}

void set_payload_meshlet_visible(inout MeshShaderVisibilityPayload payload,
                                 uint localMeshletIndex)
{
    if (localMeshletIndex < 32u)
    {
        payload.visibleMaskLo |= 1u << localMeshletIndex;
    }
    else
    {
        payload.visibleMaskHi |= 1u << (localMeshletIndex - 32u);
    }
}

bool is_meshlet_occluded_by_as_hiz(VisibleMeshlet visibleMeshlet)
{
    if (g_enableAsHiZ == 0u || g_asHiZWidth == 0u || g_asHiZHeight == 0u)
    {
        return false;
    }

    const RenderObject renderObject =
        g_renderObjects[visibleMeshlet.objectIndex];
    const MeshRange meshRange = g_meshRanges[visibleMeshlet.meshId];
    if (visibleMeshlet.meshletIndex < meshRange.firstMeshlet ||
        visibleMeshlet.meshletIndex >= meshRange.firstMeshlet + meshRange.meshletCount)
    {
        return false;
    }

    const MeshletBounds bounds = g_meshletBounds[visibleMeshlet.meshletIndex];
    const Transform transform = g_transforms[renderObject.transformId];
    const float radiusScale = transform_radius_scale_as(transform.worldMatrix);
    const float radius = max(bounds.radius * radiusScale, 0.0001f);
    const float4 worldCenter =
        mul(float4(bounds.center, 1.0f), transform.worldMatrix);
    const float4 viewCenter = mul(worldCenter, g_viewMatrix);
    if (viewCenter.z <= radius)
    {
        return false;
    }

    const float4 clipCenter = mul(worldCenter, g_viewProjectionMatrix);
    if (abs(clipCenter.w) <= 0.000001f)
    {
        return false;
    }

    const float projectedRadius =
        radius * max(abs(g_projectionMatrix[0][0]), abs(g_projectionMatrix[1][1])) /
        max(abs(viewCenter.z), 0.001f);
    const float2 ndcCenter = clipCenter.xy / clipCenter.w;
    const float2 minNdc = max(ndcCenter - projectedRadius, float2(-1.0f, -1.0f));
    const float2 maxNdc = min(ndcCenter + projectedRadius, float2(1.0f, 1.0f));
    if (minNdc.x > 1.0f || maxNdc.x < -1.0f ||
        minNdc.y > 1.0f || maxNdc.y < -1.0f)
    {
        return false;
    }

    const float2 hizSize = float2((float)g_asHiZWidth, (float)g_asHiZHeight);
    const uint2 p0 =
        min((uint2)(((minNdc * float2(0.5f, -0.5f) +
                      float2(0.5f, 0.5f)) *
                     hizSize)),
            uint2(g_asHiZWidth - 1u, g_asHiZHeight - 1u));
    const uint2 p1 =
        min((uint2)(((maxNdc * float2(0.5f, -0.5f) +
                      float2(0.5f, 0.5f)) *
                     hizSize)),
            uint2(g_asHiZWidth - 1u, g_asHiZHeight - 1u));
    const uint minX = min(p0.x, p1.x);
    const uint maxX = max(p0.x, p1.x);
    const uint minY = min(p0.y, p1.y);
    const uint maxY = max(p0.y, p1.y);
    if (maxX - minX + 1u > kAsHiZMaxRectTexels ||
        maxY - minY + 1u > kAsHiZMaxRectTexels)
    {
        return false;
    }

    const float4 nearClip =
        mul(float4(viewCenter.xyz - float3(0.0f, 0.0f, radius), 1.0f),
            g_projectionMatrix);
    if (abs(nearClip.w) <= 0.000001f)
    {
        return false;
    }
    const float meshletNearDepth = nearClip.z / nearClip.w;
    const uint2 center = uint2((minX + maxX) >> 1u, (minY + maxY) >> 1u);
    const uint2 samples[5] =
    {
        center,
        uint2(minX, minY),
        uint2(maxX, minY),
        uint2(minX, maxY),
        uint2(maxX, maxY)
    };

    [unroll]
    for (uint sampleIndex = 0u; sampleIndex < 5u; ++sampleIndex)
    {
        const float occluderDepth =
            decode_as_hiz_depth(g_asOcclusionHiZ.Load(int3(samples[sampleIndex], 0)));
        if (meshletNearDepth <= occluderDepth + kAsHiZDepthBias)
        {
            return false;
        }
    }

    return true;
}

[numthreads(64, 1, 1)]
void as_main(uint3 groupId : SV_GroupID,
             uint3 groupThreadId : SV_GroupThreadID)
{
    const uint localMeshletIndex = groupThreadId.x;
    if (localMeshletIndex == 0u)
    {
        g_asVisibleMaskLo = 0u;
        g_asVisibleMaskHi = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    const uint visibleMeshletBaseIndex =
        groupId.x * kMeshletsPerAmplificationGroup;
    const uint visibleMeshletCount =
        g_meshShaderVisibilityCounters.Load(kVisibleMeshletCounterOffset);
    const uint remainingMeshletCount =
        visibleMeshletCount > visibleMeshletBaseIndex
            ? visibleMeshletCount - visibleMeshletBaseIndex
            : 0u;
    const uint dispatchMeshletCount =
        min(remainingMeshletCount, kMeshletsPerAmplificationGroup);

    if (localMeshletIndex < dispatchMeshletCount)
    {
        const VisibleMeshlet visibleMeshlet =
            load_visible_meshlet(visibleMeshletBaseIndex + localMeshletIndex);
        if (!is_meshlet_occluded_by_as_hiz(visibleMeshlet))
        {
            if (localMeshletIndex < 32u)
            {
                InterlockedOr(g_asVisibleMaskLo, 1u << localMeshletIndex);
            }
            else
            {
                InterlockedOr(g_asVisibleMaskHi,
                              1u << (localMeshletIndex - 32u));
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();

    MeshShaderVisibilityPayload payload;
    payload.visibleMeshletBaseIndex = visibleMeshletBaseIndex;
    payload.visibleMaskLo = g_asVisibleMaskLo;
    payload.visibleMaskHi = g_asVisibleMaskHi;
    DispatchMesh(dispatchMeshletCount, 1, 1, payload);
}

[outputtopology("triangle")]
[numthreads(128, 1, 1)]
void ms_main_as(uint3 groupId : SV_GroupID,
                uint3 groupThreadId : SV_GroupThreadID,
                in payload MeshShaderVisibilityPayload payload,
                out vertices MsVertexOut vertices[kMaxOutputVertices],
                out indices uint3 triangles[kMaxOutputPrimitives],
                out primitives MsPrimitiveOut primitives[kMaxOutputPrimitives])
{
    bool active = is_payload_meshlet_visible(payload, groupId.x);
    const VisibleMeshlet visibleMeshlet =
        load_visible_meshlet(payload.visibleMeshletBaseIndex + groupId.x);

    RenderObject renderObject;
    renderObject.transformId = 0u;

    MeshRange meshRange;
    meshRange.indexCount = 0u;
    meshRange.startIndex = 0u;
    meshRange.baseVertex = 0;
    meshRange.firstMeshlet = 0u;
    meshRange.meshletCount = 0u;
    meshRange.rangeStartIndex = 0u;
    meshRange.rangeIndexCount = 0u;
    meshRange.visibilityTriangleStart = 0u;

    MeshletBounds bounds;
    bounds.firstIndex = 0u;
    bounds.indexCount = 0u;
    bounds.firstLocalIndex = 0u;
    bounds.firstSegmentVertex = 0u;

    Transform transform;
    uint selectedSegmentStartIndex = visibleMeshlet.segmentStartIndex;

    renderObject = g_renderObjects[visibleMeshlet.objectIndex];
    meshRange = g_meshRanges[visibleMeshlet.meshId];
    if (visibleMeshlet.meshletIndex < meshRange.firstMeshlet ||
        visibleMeshlet.meshletIndex >= meshRange.firstMeshlet + meshRange.meshletCount)
    {
        active = false;
    }
    else
    {
        bounds = g_meshletBounds[visibleMeshlet.meshletIndex];
        transform = g_transforms[renderObject.transformId];
    }

    const uint segmentRemainingIndexCount =
        active && bounds.indexCount > selectedSegmentStartIndex
            ? bounds.indexCount - selectedSegmentStartIndex
            : 0u;
    const uint segmentIndexCount =
        min(segmentRemainingIndexCount, kMaxOutputVertices);
    const uint primitiveCount = segmentIndexCount / 3u;
    const uint outputPrimitiveCount =
        min(primitiveCount, kMaxOutputPrimitives);
    const uint outputVertexCount = outputPrimitiveCount * 3u;
    SetMeshOutputCounts(outputVertexCount, outputPrimitiveCount);

    if (outputPrimitiveCount == 0u)
    {
        return;
    }

    for (uint vertexIndex = groupThreadId.x; vertexIndex < outputVertexCount;
         vertexIndex += 128u)
    {
        const uint localVertexIndex =
            g_rangeIndices.Load((bounds.firstIndex + selectedSegmentStartIndex +
                                 vertexIndex) *
                                4u);
        const uint sourceVertexIndex =
            (uint)(meshRange.baseVertex + (int)localVertexIndex);
        const float4 localPosition = g_positions[sourceVertexIndex];
        const float4 worldPosition = mul(localPosition, transform.worldMatrix);
        vertices[vertexIndex].position =
            mul(worldPosition, g_viewProjectionMatrix);
    }

    const uint meshPrimitiveBase =
        (bounds.firstIndex >= meshRange.rangeStartIndex)
            ? ((bounds.firstIndex - meshRange.rangeStartIndex) / 3u)
            : 0u;
    const uint meshPrimitiveSegmentOffset =
        selectedSegmentStartIndex / 3u;

    for (uint primitiveIndex = groupThreadId.x;
         primitiveIndex < outputPrimitiveCount; primitiveIndex += 128u)
    {
        triangles[primitiveIndex] =
            uint3(primitiveIndex * 3u, primitiveIndex * 3u + 1u,
                  primitiveIndex * 3u + 2u);
        primitives[primitiveIndex].renderObjectIndex =
            visibleMeshlet.objectIndex;
        primitives[primitiveIndex].meshPrimitiveId =
            meshPrimitiveBase + meshPrimitiveSegmentOffset + primitiveIndex;
        primitives[primitiveIndex].cull = false;
    }
}

[outputtopology("triangle")]
[numthreads(128, 1, 1)]
void ms_main_as_segment_remap(
    uint3 groupId : SV_GroupID,
    uint3 groupThreadId : SV_GroupThreadID,
    in payload MeshShaderVisibilityPayload payload,
    out vertices MsVertexOut vertices[kMaxOutputVertices],
    out indices uint3 triangles[kMaxOutputPrimitives],
    out primitives MsPrimitiveOut primitives[kMaxOutputPrimitives])
{
    bool active = is_payload_meshlet_visible(payload, groupId.x);
    const VisibleMeshlet visibleMeshlet =
        load_visible_meshlet(payload.visibleMeshletBaseIndex + groupId.x);

    RenderObject renderObject;
    renderObject.transformId = 0u;

    MeshRange meshRange;
    meshRange.indexCount = 0u;
    meshRange.startIndex = 0u;
    meshRange.baseVertex = 0;
    meshRange.firstMeshlet = 0u;
    meshRange.meshletCount = 0u;
    meshRange.rangeStartIndex = 0u;
    meshRange.rangeIndexCount = 0u;
    meshRange.visibilityTriangleStart = 0u;

    MeshletBounds bounds;
    bounds.firstIndex = 0u;
    bounds.indexCount = 0u;
    bounds.firstLocalIndex = 0u;
    bounds.firstSegmentVertex = 0u;

    Transform transform;
    uint selectedSegmentStartIndex = visibleMeshlet.segmentStartIndex;

    renderObject = g_renderObjects[visibleMeshlet.objectIndex];
    meshRange = g_meshRanges[visibleMeshlet.meshId];
    if (visibleMeshlet.meshletIndex < meshRange.firstMeshlet ||
        visibleMeshlet.meshletIndex >=
            meshRange.firstMeshlet + meshRange.meshletCount)
    {
        active = false;
    }
    else
    {
        bounds = g_meshletBounds[visibleMeshlet.meshletIndex];
        transform = g_transforms[renderObject.transformId];
    }

    const uint segmentRemainingIndexCount =
        active && bounds.indexCount > selectedSegmentStartIndex
            ? bounds.indexCount - selectedSegmentStartIndex
            : 0u;
    const uint segmentIndexCount =
        min(segmentRemainingIndexCount, kMaxOutputIndicesPerSegment);
    const uint primitiveCount = segmentIndexCount / 3u;
    const uint outputPrimitiveCount =
        min(primitiveCount, kMaxOutputPrimitives);
    const uint selectedSegmentIndex =
        selectedSegmentStartIndex / kMaxOutputIndicesPerSegment;
    const uint segmentVertexRange = g_meshletVertexIndices.Load(
        (bounds.firstSegmentVertex + selectedSegmentIndex) * 4u);
    const uint segmentVertexStart =
        segmentVertexRange & kSegmentVertexOffsetMask;
    const uint segmentVertexCount =
        outputPrimitiveCount == 0u
            ? 0u
            : ((segmentVertexRange >> kSegmentVertexOffsetBits) + 1u);
    const uint outputVertexCount = min(segmentVertexCount, kMaxOutputVertices);
    SetMeshOutputCounts(outputVertexCount, outputPrimitiveCount);

    if (outputPrimitiveCount == 0u)
    {
        return;
    }

    for (uint vertexIndex = groupThreadId.x; vertexIndex < outputVertexCount;
         vertexIndex += 128u)
    {
        const uint localVertexIndex = g_meshletVertexIndices.Load(
            (segmentVertexStart + vertexIndex) * 4u);
        const uint sourceVertexIndex =
            (uint)(meshRange.baseVertex + (int)localVertexIndex);
        const float4 localPosition = g_positions[sourceVertexIndex];
        const float4 worldPosition = mul(localPosition, transform.worldMatrix);
        vertices[vertexIndex].position =
            mul(worldPosition, g_viewProjectionMatrix);
    }

    const uint meshPrimitiveBase =
        (bounds.firstIndex >= meshRange.rangeStartIndex)
            ? ((bounds.firstIndex - meshRange.rangeStartIndex) / 3u)
            : 0u;
    const uint meshPrimitiveSegmentOffset = selectedSegmentStartIndex / 3u;

    for (uint primitiveIndex = groupThreadId.x;
         primitiveIndex < outputPrimitiveCount; primitiveIndex += 128u)
    {
        const uint localTriangleIndex =
            bounds.firstLocalIndex + selectedSegmentStartIndex +
            primitiveIndex * 3u;
        const uint index0 =
            load_meshlet_local_index_u8(localTriangleIndex + 0u);
        const uint index1 =
            load_meshlet_local_index_u8(localTriangleIndex + 1u);
        const uint index2 =
            load_meshlet_local_index_u8(localTriangleIndex + 2u);
        triangles[primitiveIndex] = uint3(index0, index1, index2);
        primitives[primitiveIndex].renderObjectIndex =
            visibleMeshlet.objectIndex;
        primitives[primitiveIndex].meshPrimitiveId =
            meshPrimitiveBase + meshPrimitiveSegmentOffset + primitiveIndex;
        primitives[primitiveIndex].cull =
            index0 == index1 || index1 == index2 || index2 == index0;
    }
}

[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void ms_main_as_fixed_probe(in payload MeshShaderVisibilityPayload payload,
                            out vertices MsVertexOut vertices[3],
                            out indices uint3 triangles[1],
                            out primitives MsPrimitiveOut primitives[1])
{
    SetMeshOutputCounts(3, 1);

    vertices[0].position = float4(-0.5f, -0.5f, 0.5f, 1.0f);
    vertices[1].position = float4(0.0f, 0.5f, 0.5f, 1.0f);
    vertices[2].position = float4(0.5f, -0.5f, 0.5f, 1.0f);
    triangles[0] = uint3(0u, 1u, 2u);
    primitives[0].renderObjectIndex = payload.visibleMeshletBaseIndex;
    primitives[0].meshPrimitiveId = 0u;
    primitives[0].cull = false;
}

uint ps_main_as_fixed_probe(PsInput input) : SV_Target0
{
    return input.renderObjectIndex + 1u;
}
