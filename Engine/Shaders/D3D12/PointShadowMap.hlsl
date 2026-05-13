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
        return emptyOutput;
    }

    const RenderObject renderObject = g_renderObjects[renderObjectIndex];
    const Transform transform = g_transforms[renderObject.transformId];
    const float4 worldPosition = mul(input.position, transform.worldMatrix);

    VsOut output;
    output.position =
        mul(mul(worldPosition, shadowFace.view), shadowFace.projection);
    output.position.z = output.position.z * 0.5f + output.position.w * 0.5f;
    return output;
}

void ps_main()
{}
