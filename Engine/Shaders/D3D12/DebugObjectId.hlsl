// Encode object ids into a readback-friendly target for editor picking.

#include "DrawCommon.hlsli"

struct VsIn
{
    float4 position : POSITION;
    uint4 jointIndices : JOINTS0;
    float4 weights : WEIGHTS0;
};

struct VsOut
{
    float4 position : SV_POSITION;
    nointerpolation uint objectId : TEXCOORD0;
};

cbuffer ViewProjection : register(b0)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
}

struct DrawObjectIndexConstants
{
    uint drawObjectIndex;
};

struct SelectedObjectConstants
{
    uint objectId;
};

ConstantBuffer<DrawObjectIndexConstants> g_drawObjectIndex : register(b1);
ConstantBuffer<SelectedObjectConstants> g_selectedObject : register(b2);

StructuredBuffer<RenderObject> g_renderObjects : register(t0);
StructuredBuffer<Transform> g_transforms : register(t1);
ByteAddressBuffer g_renderObjectCount : register(t2);
StructuredBuffer<SkinPalette> g_skinPalettes : register(t3);

// Vertex entry point keeps per-pass object expansion on the GPU.
VsOut vs_main(VsIn input, uint instanceId : SV_InstanceID)
{
    const uint renderObjectCount = g_renderObjectCount.Load(0);
    const uint renderObjectIndex =
        g_drawObjectIndex.drawObjectIndex + instanceId;
    if (renderObjectIndex >= renderObjectCount)
    {
        VsOut emptyOutput;
        emptyOutput.position = float4(-2.0f, -2.0f, -2.0f, 1.0f);
        emptyOutput.objectId = 0;
        return emptyOutput;
    }

    const RenderObject renderObject = g_renderObjects[renderObjectIndex];
    const Transform transform = g_transforms[renderObject.transformId];
    float4 localPosition = input.position;
    if (renderObject.skinPaletteCount > 0u)
    {
        float4x4 skinMatrix = (float4x4)0;
        skinMatrix +=
            g_skinPalettes[renderObject.skinPaletteOffset +
                input.jointIndices.x]
                .matrix *
            input.weights.x;
        skinMatrix +=
            g_skinPalettes[renderObject.skinPaletteOffset +
                input.jointIndices.y]
                .matrix *
            input.weights.y;
        skinMatrix +=
            g_skinPalettes[renderObject.skinPaletteOffset +
                input.jointIndices.z]
                .matrix *
            input.weights.z;
        skinMatrix +=
            g_skinPalettes[renderObject.skinPaletteOffset +
                input.jointIndices.w]
                .matrix *
            input.weights.w;
        localPosition = mul(localPosition, skinMatrix);
    }

    const float4 worldPosition = mul(localPosition, transform.worldMatrix);

    VsOut output;
    output.position = mul(mul(worldPosition, g_viewMatrix), g_projectionMatrix);
    output.objectId = renderObject.id + 1;
    return output;
}

// Pixel entry point writes a picking id that can be copied back as an integer.
uint ps_main(VsOut input) : SV_Target0
{
    if (g_selectedObject.objectId != 0 &&
        input.objectId != g_selectedObject.objectId)
    {
        discard;
    }

    return input.objectId;
}
