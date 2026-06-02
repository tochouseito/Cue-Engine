// Render spot-light depth into atlas space so multiple lights can share one shadow texture.

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

struct ShadowIndexConstants
{
    uint shadowIndex;
};

struct DrawObjectIndexConstants
{
    uint drawObjectIndex;
};

ConstantBuffer<DrawObjectIndexConstants> g_drawObjectIndex : register(b1);
ConstantBuffer<ShadowIndexConstants> g_shadowIndex : register(b2);

StructuredBuffer<RenderObject> g_renderObjects : register(t0);
StructuredBuffer<Transform> g_transforms : register(t1);
ByteAddressBuffer g_renderObjectCount : register(t2);
StructuredBuffer<SpotShadowFrame> g_spotShadowFrames : register(t3);
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

// GPU-side frustum rejection avoids emitting atlas draws that cannot affect the shadow tile.
bool is_shadow_caster_visible(Transform transform, SpotShadowFrame shadowFrame)
{
    const float3 center = float3(
        transform.worldMatrix._41,
        transform.worldMatrix._42,
        transform.worldMatrix._43);
    const float radius = max(
        length(float3(
            transform.worldMatrix._11,
            transform.worldMatrix._12,
            transform.worldMatrix._13)),
        max(
            length(float3(
                transform.worldMatrix._21,
                transform.worldMatrix._22,
                transform.worldMatrix._23)),
            length(float3(
                transform.worldMatrix._31,
                transform.worldMatrix._32,
                transform.worldMatrix._33)))) *
        1.7320508f;
    const float4 lightPosition =
        mul(float4(center, 1.0f), shadowFrame.view);
    const float4 clipPosition =
        mul(lightPosition, shadowFrame.projection);
    if (clipPosition.w <= 0.0001f)
    {
        return false;
    }

    const float3 ndc = clipPosition.xyz / clipPosition.w;
    const float margin =
        min(radius / max(abs(lightPosition.z), 0.001f), 1.0f);
    return ndc.x >= -1.0f - margin &&
        ndc.x <= 1.0f + margin &&
        ndc.y >= -1.0f - margin &&
        ndc.y <= 1.0f + margin &&
        ndc.z >= -1.0f - margin &&
        ndc.z <= 1.0f + margin;
}

// Vertex entry point keeps per-pass object expansion on the GPU.
VsOut vs_main(VsIn input, uint instanceId : SV_InstanceID)
{
    const uint renderObjectCount = g_renderObjectCount.Load(0);
    const uint renderObjectIndex =
        g_drawObjectIndex.drawObjectIndex + instanceId;
    const SpotShadowFrame shadowFrame =
        g_spotShadowFrames[g_shadowIndex.shadowIndex];
    if (renderObjectIndex >= renderObjectCount ||
        shadowFrame.params.x < 0.5f)
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
    if (!is_shadow_caster_visible(transform, shadowFrame))
    {
        VsOut emptyOutput;
        emptyOutput.position = float4(-2.0f, -2.0f, -2.0f, 1.0f);
        emptyOutput.shadowCasterMode = k_shadowCasterModeTwoSided;
        return emptyOutput;
    }

    const float4 localPosition =
        apply_skinning(input.position, input, renderObject);
    const float4 worldPosition = mul(localPosition, transform.worldMatrix);

    VsOut output;
    output.position =
        mul(mul(worldPosition, shadowFrame.view), shadowFrame.projection);
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
