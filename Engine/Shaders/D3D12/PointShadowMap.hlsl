// Render point-light shadow faces into an atlas layout shared by the lighting pass.

#include "DrawCommon.hlsli"

struct VsIn
{
    float4 position : POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    uint4 jointIndices : JOINTS0;
    float4 weights : WEIGHTS0;
};

struct VsOut
{
    float4 position : SV_POSITION;
    nointerpolation uint shadowCasterMode : TEXCOORD0;
};

struct DrawObjectIndexConstants
{
    uint drawObjectIndex;
};

struct PointShadowFaceConstants
{
    uint faceIndex;
};

ConstantBuffer<DrawObjectIndexConstants> g_drawObjectIndex : register(b1);
ConstantBuffer<PointShadowFaceConstants> g_pointShadowFace : register(b2);

StructuredBuffer<RenderObject> g_renderObjects : register(t0);
StructuredBuffer<Transform> g_transforms : register(t1);
ByteAddressBuffer g_renderObjectCount : register(t2);
StructuredBuffer<PointShadowFace> g_pointShadowFaces : register(t3);
StructuredBuffer<SkinPalette> g_skinPalettes : register(t4);

// Skinning is evaluated here so shadow passes use the same deformed positions as color passes.
float4 apply_skinning(float4 position, VsIn input, RenderObject renderObject)
{
    if (renderObject.skinPaletteCount == 0u)
    {
        return position;
    }

    float4x4 skinMatrix = (float4x4)0;
    skinMatrix +=
        g_skinPalettes[renderObject.skinPaletteOffset + input.jointIndices.x]
            .matrix *
        input.weights.x;
    skinMatrix +=
        g_skinPalettes[renderObject.skinPaletteOffset + input.jointIndices.y]
            .matrix *
        input.weights.y;
    skinMatrix +=
        g_skinPalettes[renderObject.skinPaletteOffset + input.jointIndices.z]
            .matrix *
        input.weights.z;
    skinMatrix +=
        g_skinPalettes[renderObject.skinPaletteOffset + input.jointIndices.w]
            .matrix *
        input.weights.w;
    return mul(position, skinMatrix);
}

// Vertex entry point keeps per-pass object expansion on the GPU.
VsOut vs_main(VsIn input, uint instanceId : SV_InstanceID)
{
    const uint renderObjectCount = g_renderObjectCount.Load(0);
    const uint renderObjectIndex =
        g_drawObjectIndex.drawObjectIndex + instanceId;
    const PointShadowFace shadowFace =
        g_pointShadowFaces[g_pointShadowFace.faceIndex];
    if (renderObjectIndex >= renderObjectCount || shadowFace.params.x < 0.5f)
    {
        VsOut emptyOutput;
        emptyOutput.position = float4(-2.0f, -2.0f, -2.0f, 1.0f);
        emptyOutput.shadowCasterMode = k_shadowCasterModeTwoSided;
        return emptyOutput;
    }

    const RenderObject renderObject = g_renderObjects[renderObjectIndex];
    if (renderObject.castsShadow == 0u)
    {
        VsOut emptyOutput;
        emptyOutput.position = float4(-2.0f, -2.0f, -2.0f, 1.0f);
        emptyOutput.shadowCasterMode = k_shadowCasterModeTwoSided;
        return emptyOutput;
    }

    const Transform transform = g_transforms[renderObject.transformId];
    const float4 localPosition =
        apply_skinning(input.position, input, renderObject);
    const float4 worldPosition = mul(localPosition, transform.worldMatrix);

    VsOut output;
    output.position =
        mul(mul(worldPosition, shadowFace.view), shadowFace.projection);
    output.position.z = output.position.z * 0.5f + output.position.w * 0.5f;
    output.shadowCasterMode = renderObject.shadowCasterMode;
    return output;
}

// Pixel entry point can discard unsupported faces while keeping depth writes hardware-driven.
void ps_main(VsOut input, bool isFrontFace : SV_IsFrontFace)
{
    if (input.shadowCasterMode == k_shadowCasterModeSolid && !isFrontFace)
    {
        discard;
    }
}
