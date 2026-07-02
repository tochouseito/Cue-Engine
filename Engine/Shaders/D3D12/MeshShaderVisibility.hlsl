// Mesh shader visibility buffer path.

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
    uint firstLocalIndex;
    uint firstSegmentVertex;
};

struct MsVertexOut
{
    float4 position : SV_POSITION;
};

struct MsPrimitiveOut
{
    nointerpolation uint renderObjectIndex : TEXCOORD0;
    nointerpolation uint meshPrimitiveId : TEXCOORD1;
    bool cull : SV_CullPrimitive;
};

struct PsInput
{
    float4 position : SV_POSITION;
    nointerpolation uint renderObjectIndex : TEXCOORD0;
    nointerpolation uint meshPrimitiveId : TEXCOORD1;
};

cbuffer ViewProjection : register(b0)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
    row_major float4x4 g_viewProjectionMatrix;
};

StructuredBuffer<RenderObject> g_renderObjects : register(t0);
StructuredBuffer<Transform> g_transforms : register(t1);
#ifdef CUE_MESH_SHADER_VISIBILITY_EXTERNAL_VISIBLE_MESHLETS
ByteAddressBuffer g_visibleMeshletsRaw : register(t2);
#else
StructuredBuffer<VisibleMeshlet> g_visibleMeshlets : register(t2);
#endif
StructuredBuffer<MeshRange> g_meshRanges : register(t3);
StructuredBuffer<MeshletBounds> g_meshletBounds : register(t4);
ByteAddressBuffer g_rangeIndices : register(t5);
ByteAddressBuffer g_meshletLocalIndices : register(t6);
ByteAddressBuffer g_meshletVertexIndices : register(t7);
StructuredBuffer<float4> g_positions : register(t8);

#ifndef CUE_MESH_SHADER_VISIBILITY_MAX_OUTPUT_VERTICES
#define CUE_MESH_SHADER_VISIBILITY_MAX_OUTPUT_VERTICES 255u
#endif
#ifndef CUE_MESH_SHADER_VISIBILITY_MAX_OUTPUT_PRIMITIVES
#define CUE_MESH_SHADER_VISIBILITY_MAX_OUTPUT_PRIMITIVES 128u
#endif
static const uint kMaxOutputVertices = CUE_MESH_SHADER_VISIBILITY_MAX_OUTPUT_VERTICES;
static const uint kMaxOutputPrimitives = CUE_MESH_SHADER_VISIBILITY_MAX_OUTPUT_PRIMITIVES;
static const uint kMaxOutputIndicesPerSegment = kMaxOutputPrimitives * 3u;
static const uint kSegmentVertexOffsetBits = 26u;
static const uint kSegmentVertexOffsetMask = (1u << kSegmentVertexOffsetBits) - 1u;
static const uint kVisibilityPrimitiveBits = 19u;
static const uint kVisibilityPrimitiveMask = (1u << kVisibilityPrimitiveBits) - 1u;
static const uint kVisibilityObjectShift = kVisibilityPrimitiveBits;
static const uint kVisibilityMaxObjectId = (1u << (32u - kVisibilityPrimitiveBits)) - 1u;

VisibleMeshlet load_visible_meshlet(uint visibleMeshletIndex)
{
#ifdef CUE_MESH_SHADER_VISIBILITY_EXTERNAL_VISIBLE_MESHLETS
    const uint4 packed = g_visibleMeshletsRaw.Load4(visibleMeshletIndex * 16u);
    VisibleMeshlet visibleMeshlet;
    visibleMeshlet.objectIndex = packed.x;
    visibleMeshlet.meshId = packed.y;
    visibleMeshlet.meshletIndex = packed.z;
    visibleMeshlet.segmentStartIndex = packed.w;
    return visibleMeshlet;
#else
    return g_visibleMeshlets[visibleMeshletIndex];
#endif
}

uint load_meshlet_local_index_u8(uint localIndexByteOffset)
{
    const uint packed = g_meshletLocalIndices.Load(localIndexByteOffset & ~3u);
    return (packed >> ((localIndexByteOffset & 3u) * 8u)) & 0xffu;
}

#ifndef CUE_MESH_SHADER_VISIBILITY_NO_DIRECT_MS
[outputtopology("triangle")]
[numthreads(128, 1, 1)]
void ms_main(uint3 groupId : SV_GroupID,
             uint3 groupThreadId : SV_GroupThreadID,
             out vertices MsVertexOut vertices[kMaxOutputVertices],
             out indices uint3 triangles[kMaxOutputPrimitives],
             out primitives MsPrimitiveOut primitives[kMaxOutputPrimitives])
{
    bool active = true;
    const VisibleMeshlet visibleMeshlet = load_visible_meshlet(groupId.x);

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
void ms_main_segment_remap(uint3 groupId : SV_GroupID,
                           uint3 groupThreadId : SV_GroupThreadID,
                           out vertices MsVertexOut vertices[kMaxOutputVertices],
                           out indices uint3 triangles[kMaxOutputPrimitives],
                           out primitives MsPrimitiveOut primitives[kMaxOutputPrimitives])
{
    bool active = true;
    const VisibleMeshlet visibleMeshlet = load_visible_meshlet(groupId.x);

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
    const uint meshPrimitiveSegmentOffset =
        selectedSegmentStartIndex / 3u;

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
#endif

uint ps_main(PsInput input) : SV_Target0
{
    const uint objectId = input.renderObjectIndex + 1u;
    if (objectId == 0u || objectId > kVisibilityMaxObjectId ||
        input.meshPrimitiveId > kVisibilityPrimitiveMask)
    {
        return 0u;
    }

    return (objectId << kVisibilityObjectShift) | input.meshPrimitiveId;
}

uint ps_depth_only(PsInput input) : SV_Target0
{
    (void)input;
    return 0u;
}
