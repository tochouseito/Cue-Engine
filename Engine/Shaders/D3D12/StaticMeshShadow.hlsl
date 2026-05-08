#include "DrawCommon.hlsli"

struct VsIn
{
    float4 position : POSITION;
};

struct VsOut
{
    float4 position : SV_POSITION;
    float depth : TEXCOORD0;
};

cbuffer ShadowMapping : register(b0)
{
    row_major float4x4 g_shadowViewMatrix;
    row_major float4x4 g_shadowProjectionMatrix;
    float4 g_shadowTexelSizeAndBias;
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
    if (renderObjectIndex >= renderObjectCount)
    {
        VsOut emptyOutput;
        emptyOutput.position = float4(-2.0f, -2.0f, -2.0f, 1.0f);
        emptyOutput.depth = 1.0f;
        return emptyOutput;
    }

    const RenderObject renderObject = g_renderObjects[renderObjectIndex];
    const Transform transform = g_transforms[renderObject.transformId];
    const float4 worldPosition = mul(input.position, transform.worldMatrix);
    const float4 lightPosition =
        mul(mul(worldPosition, g_shadowViewMatrix), g_shadowProjectionMatrix);

    VsOut output;
    output.position = lightPosition;
    output.depth = saturate(lightPosition.z / max(lightPosition.w, 0.0001f));
    return output;
}

float ps_main(VsOut input) : SV_Target0
{
    return input.depth;
}
