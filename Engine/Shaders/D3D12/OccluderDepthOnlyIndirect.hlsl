// Early occlusion 用 depth-only indirect pass。
// ObjectCullAndLod が選んだ価値のある occluder だけを描き、BuildHiZDepth が
// 後続の object/cell culling 用 Hi-Z depth を作れるようにする。

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
StructuredBuffer<uint> g_renderObjectIndices : register(t5);

VsOutput vs_main(VsInput input, uint instanceId : SV_InstanceID)
{
    const uint renderObjectIndex =
        g_renderObjectIndices[g_drawObjectIndex.drawObjectIndex + instanceId];
    const RenderObject renderObject = g_renderObjects[renderObjectIndex];
    const Transform transform = g_transforms[renderObject.transformId];

    VsOutput output;
    // LOD4/impostor は billboard として depth を出す。
    // 遠距離の小さい mesh を高ポリゴンのまま occluder にしないための軽量 path。
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

    // depth-only なので normal/uv/material/light は読まず、position と transform だけ使う。
    const float4 worldPosition =
        mul(float4(input.position.xyz, 1.0f), transform.worldMatrix);
    output.position = mul(mul(worldPosition, g_viewMatrix), g_projectionMatrix);
    return output;
}

void ps_main(VsOutput input)
{
}
