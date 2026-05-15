#include "DrawCommon.hlsli"

struct VsIn
{
    float4 position : POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

struct VsOut
{
    float4 position : SV_POSITION;
};

cbuffer DirectionalShadowFrame : register(b0)
{
    row_major float4x4 g_directionalShadowView;
    row_major float4x4 g_directionalShadowProjection;
    float4 g_directionalShadowParams;
    float4 g_directionalShadowTuning;
}

struct DrawObjectIndexConstants
{
    uint drawObjectIndex;
};

ConstantBuffer<DrawObjectIndexConstants> g_drawObjectIndex : register(b1);

StructuredBuffer<RenderObject> g_renderObjects : register(t0);
StructuredBuffer<Transform> g_transforms : register(t1);
ByteAddressBuffer g_renderObjectCount : register(t2);

VsOut vs_main(VsIn input, uint instanceId : SV_InstanceID)
{
    const uint renderObjectCount = g_renderObjectCount.Load(0);
    const uint renderObjectIndex =
        g_drawObjectIndex.drawObjectIndex + instanceId;
    if (renderObjectIndex >= renderObjectCount ||
        g_directionalShadowParams.x < 0.5f)
    {
        VsOut emptyOutput;
        emptyOutput.position = float4(-2.0f, -2.0f, -2.0f, 1.0f);
        return emptyOutput;
    }

    const RenderObject renderObject = g_renderObjects[renderObjectIndex];
    if (renderObject.castsShadow == 0u)
    {
        VsOut emptyOutput;
        emptyOutput.position = float4(-2.0f, -2.0f, -2.0f, 1.0f);
        return emptyOutput;
    }

    const Transform transform = g_transforms[renderObject.transformId];
    const float4 worldPosition = mul(input.position, transform.worldMatrix);

    VsOut output;
    output.position =
        mul(mul(worldPosition, g_directionalShadowView),
            g_directionalShadowProjection);
    return output;
}

void ps_main()
{}
