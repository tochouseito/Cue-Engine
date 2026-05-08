#include "DrawCommon.hlsli"

struct VsOut
{
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD2;
    float3 worldNormal : NORMAL0;
    float2 texcoord : TEXCOORD0;
    nointerpolation uint materialId : TEXCOORD1;
};

struct VsIn
{
    float4 position : POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

static const float k_pi = 3.14159265359f;

cbuffer ViewProjection : register(b0)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
    float4 g_cameraPosition;
}

cbuffer DirectionalLight : register(b2)
{
    float4 g_lightDirectionAndIntensity;
    float4 g_lightColorAndAmbient;
    float4 g_ambientGroundAndSpecular;
}

cbuffer ShadowMapping : register(b3)
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
StructuredBuffer<Material> g_materials : register(t3);
Texture2D<float4> g_textures[] : register(t0, space1);
Texture2D<float> g_shadowMap : register(t0, space2);

VsOut vs_main(VsIn input, uint instanceId : SV_InstanceID)
{
    const uint renderObjectCount = g_renderObjectCount.Load(0);
    const uint renderObjectIndex =
        g_drawObjectIndex.drawObjectIndex + instanceId;
    if (renderObjectIndex >= renderObjectCount)
    {
        VsOut emptyOutput;
        emptyOutput.position = float4(-2.0f, -2.0f, -2.0f, 1.0f);
        emptyOutput.worldPosition = float3(0.0f, 0.0f, 0.0f);
        emptyOutput.worldNormal = float3(0.0f, 0.0f, 1.0f);
        emptyOutput.texcoord = float2(0.0f, 0.0f);
        emptyOutput.materialId = 0;
        return emptyOutput;
    }

    const RenderObject renderObject = g_renderObjects[renderObjectIndex];
    const Transform transform = g_transforms[renderObject.transformId];
    const float4 localPosition = input.position;
    const float2 localUv = input.texcoord;
    const float3 localNormal = input.normal;

    const float4 worldPosition = mul(localPosition, transform.worldMatrix);
    const float3 worldNormal =
        normalize(mul(float4(localNormal, 0.0f), transform.normalMatrix).xyz);

    VsOut output;
    output.position = mul(mul(worldPosition, g_viewMatrix), g_projectionMatrix);
    output.worldPosition = worldPosition.xyz;
    output.worldNormal = worldNormal;
    output.texcoord = localUv;
    output.materialId = renderObject.materialId;
    return output;
}

float calculate_shadow_visibility(float3 worldPosition, float ndotl)
{
    const float4 lightPosition =
        mul(mul(float4(worldPosition, 1.0f), g_shadowViewMatrix),
            g_shadowProjectionMatrix);
    const float3 shadowPosition = lightPosition.xyz / lightPosition.w;
    if (shadowPosition.x < -1.0f || shadowPosition.x > 1.0f ||
        shadowPosition.y < -1.0f || shadowPosition.y > 1.0f ||
        shadowPosition.z < 0.0f || shadowPosition.z > 1.0f ||
        ndotl <= 0.0001f)
    {
        return 1.0f;
    }

    const float2 shadowUv =
        float2(shadowPosition.x * 0.5f + 0.5f,
            -shadowPosition.y * 0.5f + 0.5f);
    uint shadowWidth = 1;
    uint shadowHeight = 1;
    g_shadowMap.GetDimensions(shadowWidth, shadowHeight);

    const float2 texelSize = g_shadowTexelSizeAndBias.xy;
    const float bias = max(g_shadowTexelSizeAndBias.z * (1.0f - ndotl), 0.001f);
    float visibility = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const float2 sampleUv =
                saturate(shadowUv + float2((float)x, (float)y) * texelSize);
            const uint2 sampleCoord = uint2(
                min((uint)(sampleUv.x * shadowWidth), shadowWidth - 1),
                min((uint)(sampleUv.y * shadowHeight), shadowHeight - 1));
            const float closestDepth =
                g_shadowMap.Load(int3(sampleCoord, 0));
            visibility +=
                (shadowPosition.z - bias <= closestDepth) ? 1.0f : 0.0f;
        }
    }

    visibility /= 9.0f;
    const float shadowStrength = saturate(g_shadowTexelSizeAndBias.w);
    return lerp(1.0f - shadowStrength, 1.0f, visibility);
}

float4 ps_main(VsOut input) : SV_Target0
{
    const float3 lightDirection = normalize(g_lightDirectionAndIntensity.xyz);
    const float lightIntensity = max(g_lightDirectionAndIntensity.w, 0.0f);
    const float3 lightColor = max(g_lightColorAndAmbient.rgb, 0.0f);
    const float ambientStrength = max(g_lightColorAndAmbient.a, 0.0f);
    const float3 groundAmbient = max(g_ambientGroundAndSpecular.rgb, 0.0f);
    const float3 viewDirection =
        normalize(g_cameraPosition.xyz - input.worldPosition);
    float3 worldNormal = normalize(input.worldNormal);
    if (dot(worldNormal, viewDirection) < 0.0f)
    {
        worldNormal = -worldNormal;
    }
    const float ndotl = saturate(dot(worldNormal, -lightDirection));
    const Material material = g_materials[input.materialId];
    float3 textureColor = float3(1.0f, 1.0f, 1.0f);
    if (material.useTexture != 0)
    {
        const uint textureIndex = NonUniformResourceIndex(material.textureId);
        uint textureWidth = 1;
        uint textureHeight = 1;
        g_textures[textureIndex].GetDimensions(textureWidth, textureHeight);
        const float2 wrappedUv = frac(input.texcoord);
        const uint2 texelCoord = uint2(
            min((uint)(wrappedUv.x * textureWidth), textureWidth - 1),
            min((uint)(wrappedUv.y * textureHeight), textureHeight - 1));
        textureColor =
            g_textures[textureIndex].Load(int3(texelCoord, 0)).rgb;
    }

    const float3 baseColor = material.color.rgb * textureColor;
    const float skyAmount = saturate(worldNormal.y * 0.5f + 0.5f);
    const float3 hemisphereAmbient =
        lerp(groundAmbient, lightColor, skyAmount) * ambientStrength;
    const float3 ambientFloor = lightColor * ambientStrength * 0.65f;
    const float3 ambientColor = max(hemisphereAmbient, ambientFloor);
    const float diffuseStrength = max(material.diffuseStrength, 0.0f);
    const float shadowVisibility =
        calculate_shadow_visibility(input.worldPosition, ndotl);
    const float fillAmount = 0.12f * (1.0f - ndotl);
    const float3 diffuse =
        lightColor * lightIntensity * diffuseStrength *
        (ndotl * shadowVisibility + fillAmount);

    const float3 halfVector = normalize(-lightDirection + viewDirection);
    const float specularPower = max(material.shininess, 1.0f);
    const float specularAmount =
        pow(saturate(dot(worldNormal, halfVector)), specularPower) *
        max(material.specularStrength, 0.0f) *
        lightIntensity *
        shadowVisibility *
        step(0.0001f, ndotl);
    const float3 specular = lightColor * specularAmount;
    const float3 color = baseColor * (ambientColor + diffuse) + specular;
    return float4(color, 1.0f);
}
