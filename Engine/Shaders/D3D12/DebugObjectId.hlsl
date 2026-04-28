#include "DrawCommon.hlsli"

struct VsIn
{
    float4 position : POSITION;
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
        emptyOutput.objectId = 0;
        return emptyOutput;
    }

    const RenderObject renderObject = g_renderObjects[renderObjectIndex];
    const Transform transform = g_transforms[renderObject.transformId];
    const float4 worldPosition = mul(input.position, transform.worldMatrix);

    VsOut output;
    output.position = mul(mul(worldPosition, g_viewMatrix), g_projectionMatrix);
    output.objectId = renderObject.id + 1;
    return output;
}

uint ps_main(VsOut input) : SV_Target0
{
    return input.objectId;
}
