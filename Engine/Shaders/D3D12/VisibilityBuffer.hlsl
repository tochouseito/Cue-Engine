// Visibility buffer pass.
// Writes the visible render object index and mesh-local primitive id only.

#include "GeneratedMeshletIndexCommon.hlsli"

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

struct VsInput
{
    float4 position : POSITION;
};

struct VsOutput
{
    float4 position : SV_POSITION;
    nointerpolation uint renderObjectIndex : TEXCOORD0;
    nointerpolation uint primitiveBase : TEXCOORD1;
    nointerpolation uint outputPrimitiveBase : TEXCOORD2;
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
    uint padding0;
    uint padding1;
};

struct VisibleMeshlet
{
    uint objectIndex;
    uint meshId;
    uint meshletIndex;
    uint segmentStartIndex;
};

cbuffer ViewProjection : register(b0)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
};

struct DrawObjectIndexConstants
{
    uint drawObjectIndex;
    uint primitiveBase;
};

ConstantBuffer<DrawObjectIndexConstants> g_drawObjectIndex : register(b1);

StructuredBuffer<RenderObject> g_renderObjects : register(t0);
StructuredBuffer<Transform> g_transforms : register(t1);
StructuredBuffer<uint> g_renderObjectIndices : register(t6);
StructuredBuffer<VisibleMeshlet> g_visibleMeshlets : register(t10);
StructuredBuffer<MeshletBounds> g_meshletBounds : register(t11);
StructuredBuffer<uint> g_meshletVertexIndices : register(t12);
StructuredBuffer<uint> g_generatedIndexOffsets : register(t13);
StructuredBuffer<MeshRange> g_meshRanges : register(t14);
StructuredBuffer<float4> g_positions : register(t15);

static const uint kVisibilityPrimitiveBits = 19u;
static const uint kVisibilityPrimitiveMask = (1u << kVisibilityPrimitiveBits) - 1u;
static const uint kVisibilityObjectShift = kVisibilityPrimitiveBits;
static const uint kVisibilityMaxObjectId = (1u << (32u - kVisibilityPrimitiveBits)) - 1u;

VsOutput build_vs_output(VsInput input, uint renderObjectIndex, uint primitiveBase)
{
    const RenderObject renderObject = g_renderObjects[renderObjectIndex];
    const Transform transform = g_transforms[renderObject.transformId];

    VsOutput output;
    output.renderObjectIndex = renderObjectIndex;
    output.primitiveBase = primitiveBase;
    output.outputPrimitiveBase = 0u;

    if ((renderObject.drawFlags & 1u) != 0u)
    {
        const float4 worldCenter =
            float4(renderObject.boundsCenterRadius.xyz, 1.0f);
        const float objectScale = length(transform.worldMatrix[0].xyz);
        float4 viewPosition = mul(worldCenter, g_viewMatrix);
        viewPosition.xy += input.position.xy * objectScale;

        output.position = mul(viewPosition, g_projectionMatrix);
        return output;
    }

    const float4 worldPosition = mul(input.position, transform.worldMatrix);
    const float4 viewPosition = mul(worldPosition, g_viewMatrix);

    output.position = mul(viewPosition, g_projectionMatrix);
    return output;
}

VsOutput vs_main(VsInput input, uint instanceId : SV_InstanceID)
{
    const uint renderObjectIndex =
        g_renderObjectIndices[g_drawObjectIndex.drawObjectIndex + instanceId];
    return build_vs_output(input, renderObjectIndex,
                           g_drawObjectIndex.primitiveBase);
}

VsOutput range_vs_main(VsInput input, uint instanceId : SV_InstanceID)
{
    (void)instanceId;
    return build_vs_output(input, g_drawObjectIndex.drawObjectIndex,
                           g_drawObjectIndex.primitiveBase);
}

VsOutput generated_vs_main(uint virtualVertexId : SV_VertexID)
{
    const uint visibleIndex =
        virtualVertexId / kGeneratedMaxVerticesPerMeshlet;
    const uint localVertexIndex =
        virtualVertexId % kGeneratedMaxVerticesPerMeshlet;
    const VisibleMeshlet visible = g_visibleMeshlets[visibleIndex];
    const MeshletBounds bounds = g_meshletBounds[visible.meshletIndex];
    const MeshRange meshRange = g_meshRanges[visible.meshId];
    const uint segmentIndex = visible.segmentStartIndex / 384u;
    const uint packedVertexRange =
        g_meshletVertexIndices[bounds.padding1 + segmentIndex];
    const uint sourceVertexStart = packedVertexRange & ((1u << 26u) - 1u);
    const uint sourceVertexIndex =
        (uint)(meshRange.baseVertex +
               (int)g_meshletVertexIndices[sourceVertexStart +
                                            localVertexIndex]);

    VsInput input;
    input.position = g_positions[sourceVertexIndex];
    const uint sourcePrimitiveBase =
        (bounds.firstIndex - meshRange.rangeStartIndex +
         visible.segmentStartIndex) /
        3u;
    VsOutput output =
        build_vs_output(input, visible.objectIndex, sourcePrimitiveBase);
    output.outputPrimitiveBase = g_generatedIndexOffsets[visibleIndex] / 3u;
    return output;
}

uint ps_main(VsOutput input, uint primitiveId : SV_PrimitiveID) : SV_Target0
{
    const uint objectId = input.renderObjectIndex + 1u;
    if (primitiveId < input.outputPrimitiveBase)
    {
        return 0u;
    }
    const uint meshPrimitiveId =
        input.primitiveBase + primitiveId - input.outputPrimitiveBase;
    if (objectId == 0u || objectId > kVisibilityMaxObjectId ||
        meshPrimitiveId > kVisibilityPrimitiveMask)
    {
        return 0u;
    }

    return (objectId << kVisibilityObjectShift) | meshPrimitiveId;
}
