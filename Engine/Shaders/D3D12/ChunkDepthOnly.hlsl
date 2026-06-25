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
StructuredBuffer<uint> g_chunkDepthInstanceList : register(t2);

float4 vs_main(VsInput input, uint instanceId : SV_InstanceID) : SV_Position
{
    const uint renderObjectIndex =
        g_chunkDepthInstanceList[g_drawObjectIndex.drawObjectIndex + instanceId];
    const RenderObject renderObject = g_renderObjects[renderObjectIndex];
    const Transform transform = g_transforms[renderObject.transformId];

    const float4 worldPosition = mul(input.position, transform.worldMatrix);
    const float4 viewPosition = mul(worldPosition, g_viewMatrix);
    return mul(viewPosition, g_projectionMatrix);
}

void ps_main()
{
}
