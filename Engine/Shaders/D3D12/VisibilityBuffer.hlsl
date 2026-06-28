// Visibility buffer pass.
// Writes the visible render object index and mesh-local primitive id only.

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
};

cbuffer ViewProjection : register(b0)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
};

struct DrawObjectIndexConstants
{
    uint drawObjectIndex;
};

ConstantBuffer<DrawObjectIndexConstants> g_drawObjectIndex : register(b1);

StructuredBuffer<RenderObject> g_renderObjects : register(t0);
StructuredBuffer<Transform> g_transforms : register(t1);
StructuredBuffer<uint> g_renderObjectIndices : register(t6);

VsOutput build_vs_output(VsInput input, uint renderObjectIndex)
{
    const RenderObject renderObject = g_renderObjects[renderObjectIndex];
    const Transform transform = g_transforms[renderObject.transformId];

    VsOutput output;
    output.renderObjectIndex = renderObjectIndex;

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
    return build_vs_output(input, renderObjectIndex);
}

VsOutput range_vs_main(VsInput input)
{
    return build_vs_output(input, g_drawObjectIndex.drawObjectIndex);
}

uint2 ps_main(VsOutput input, uint primitiveId : SV_PrimitiveID) : SV_Target0
{
    return uint2(input.renderObjectIndex + 1u, primitiveId);
}
