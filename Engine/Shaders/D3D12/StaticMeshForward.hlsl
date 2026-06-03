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
};

struct Transform
{
    row_major float4x4 worldMatrix;
    row_major float4x4 normalMatrix;
};

struct Material
{
    float4 color;
    uint textureId;
    uint useTexture;
    uint useReflectionSkybox;
    float shininess;
};

struct LightFrame
{
    float4 ambientColorIntensity;
    uint directionalLightCount;
    uint pointLightCount;
    uint spotLightCount;
    uint padding;
};

struct PointLight
{
    float4 positionRange;
    float4 colorIntensity;
};

struct VsInput
{
    float4 position : POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

struct VsOutput
{
    float4 position : SV_POSITION;
    float3 worldPosition : POSITION0;
    float3 worldNormal : NORMAL0;
    float2 texcoord : TEXCOORD0;
    nointerpolation uint materialId : TEXCOORD1;
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
ByteAddressBuffer g_renderObjectCount : register(t2);
StructuredBuffer<Material> g_materials : register(t3);
ConstantBuffer<LightFrame> g_lightFrame : register(b2);
StructuredBuffer<PointLight> g_pointLights : register(t4);

VsOutput vs_main(VsInput input, uint instanceId : SV_InstanceID)
{
    // ExecuteIndirect 側の instanceCount を信用する
    const uint renderObjectIndex =
        g_drawObjectIndex.drawObjectIndex + instanceId;

    // 描画対象情報を取得する
    const RenderObject renderObject = g_renderObjects[renderObjectIndex];
    const Transform transform = g_transforms[renderObject.transformId];

    // 頂点変換
    const float4 worldPosition = mul(input.position, transform.worldMatrix);
    const float3 worldNormal =
        normalize(mul(float4(input.normal, 0.0f), transform.normalMatrix).xyz);

    VsOutput output;
    output.position = mul(mul(worldPosition, g_viewMatrix), g_projectionMatrix);
    output.worldPosition = worldPosition.xyz;
    output.worldNormal = worldNormal;
    output.texcoord = input.texcoord;
    output.materialId = renderObject.materialId;

    return output;
}

float3 evaluate_point_lighting(float3 worldPosition, float3 worldNormal)
{
    float3 lighting =
        g_lightFrame.ambientColorIntensity.rgb *
        g_lightFrame.ambientColorIntensity.a;

    const uint pointCount = g_lightFrame.pointLightCount;
    for (uint lightIndex = 0; lightIndex < pointCount; ++lightIndex)
    {
        const PointLight light = g_pointLights[lightIndex];
        const float3 toLight = light.positionRange.xyz - worldPosition;
        const float distance = length(toLight);
        const float range = max(light.positionRange.w, 0.001f);
        const float3 lightDirection =
            distance > 0.0001f ? toLight / distance : float3(0.0f, 1.0f, 0.0f);
        const float attenuation = pow(saturate(1.0f - distance / range), 2.0f);
        const float diffuse = saturate(dot(worldNormal, lightDirection));

        lighting +=
            light.colorIntensity.rgb *
            light.colorIntensity.w *
            attenuation *
            diffuse;
    }

    return lighting;
}

float4 ps_main(VsOutput input) : SV_Target0
{
    const Material material = g_materials[input.materialId];
    const float3 normal = normalize(input.worldNormal);
    const float3 lighting =
        max(evaluate_point_lighting(input.worldPosition, normal), 0.0f);
    return float4(material.color.rgb * lighting, material.color.a);
}
