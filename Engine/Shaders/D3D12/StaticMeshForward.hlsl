// Render static and skinned meshes with material, lighting, and shadow sampling in one forward pass.

#include "DrawCommon.hlsli"

struct VsOut
{
    float4 position : SV_POSITION;
    float3 worldPosition : POSITION0;
    float3 worldNormal : NORMAL0;
    float2 texcoord : TEXCOORD0;
    nointerpolation uint materialId : TEXCOORD1;
    nointerpolation uint receivesShadow : TEXCOORD2;
};

struct VsIn
{
    float4 position : POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    uint4 jointIndices : JOINTS0;
    float4 weights : WEIGHTS0;
};

static const float k_pi = 3.14159265359f;
static const uint k_maxSpotShadowCount = 4u;
static const uint k_pointShadowFaceCount = 6u;
static const uint k_debugViewShadingSolid = 0u;
static const uint k_debugViewShadingMaterial = 1u;
static const uint k_debugViewShadingLighting = 2u;
static const uint k_debugViewShadingMaterialLighting = 3u;
static const float3 k_solidColor = float3(0.8f, 0.8f, 0.8f);

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
StructuredBuffer<Material> g_materials : register(t3);
ConstantBuffer<LightFrame> g_lightFrame : register(b2);
StructuredBuffer<DirectionalLight> g_directionalLights : register(t4);
StructuredBuffer<PointLight> g_pointLights : register(t5);
StructuredBuffer<SpotLight> g_spotLights : register(t6);
StructuredBuffer<SpotShadowFrame> g_spotShadowFrames : register(t7);
Texture2D<float> g_spotShadowMap : register(t8);
ConstantBuffer<DirectionalShadowFrame> g_directionalShadowFrame : register(b3);
Texture2D<float> g_directionalShadowMap : register(t9);
StructuredBuffer<PointShadowFace> g_pointShadowFaces : register(t10);
Texture2D<float> g_pointShadowMap : register(t11);
TextureCube<float4> g_reflectionSkybox : register(t12);
StructuredBuffer<SkinPalette> g_skinPalettes : register(t13);
Texture2D<float4> g_textures[] : register(t0, space1);
SamplerState g_sampler : register(s0);

// Bias combines slope and screen-space depth gradient to reduce acne without over-detaching shadows.
float shadow_receiver_bias(
    float3 worldNormal,
    float3 surfaceToLight,
    float baseBias,
    float slopeBias,
    float ndcDepth)
{
    const float angular = 1.0f - saturate(dot(worldNormal, surfaceToLight));
    const float depthGradient =
        max(abs(ddx(ndcDepth)), abs(ddy(ndcDepth)));
    const float biasLimit = baseBias + slopeBias;
    const float gradientBias =
        min(depthGradient * 0.25f, biasLimit * 2.0f);
    return min(
        baseBias + slopeBias * angular * angular + gradientBias,
        biasLimit * 4.0f);
}

struct DebugViewShadingConstants
{
    uint mode;
};

ConstantBuffer<DebugViewShadingConstants> g_debugViewShading : register(b4);

struct ReflectionConstants
{
    uint hasSkybox;
};

ConstantBuffer<ReflectionConstants> g_reflection : register(b5);

// Vertex entry point keeps per-pass object expansion on the GPU.
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
    float4 localPosition = input.position;
    const float2 localUv = input.texcoord;
    float3 localNormal = input.normal;
    if (renderObject.skinPaletteCount > 0u)
    {
        float4x4 skinMatrix = (float4x4)0;
        skinMatrix +=
            g_skinPalettes[renderObject.skinPaletteOffset +
                input.jointIndices.x]
                .matrix *
            input.weights.x;
        skinMatrix +=
            g_skinPalettes[renderObject.skinPaletteOffset +
                input.jointIndices.y]
                .matrix *
            input.weights.y;
        skinMatrix +=
            g_skinPalettes[renderObject.skinPaletteOffset +
                input.jointIndices.z]
                .matrix *
            input.weights.z;
        skinMatrix +=
            g_skinPalettes[renderObject.skinPaletteOffset +
                input.jointIndices.w]
                .matrix *
            input.weights.w;
        localPosition = mul(localPosition, skinMatrix);
        localNormal = normalize(mul(float4(localNormal, 0.0f), skinMatrix).xyz);
    }

    const float4 worldPosition = mul(localPosition, transform.worldMatrix);
    const float3 worldNormal =
        normalize(mul(float4(localNormal, 0.0f), transform.normalMatrix).xyz);

    VsOut output;
    output.position = mul(mul(worldPosition, g_viewMatrix), g_projectionMatrix);
    output.worldPosition = worldPosition.xyz;
    output.worldNormal = worldNormal;
    output.texcoord = localUv;
    output.materialId = renderObject.materialId;
    output.receivesShadow = renderObject.receivesShadow;
    return output;
}

// Spot shadow sampling stays local to the lighting shader so atlas layout remains a GPU contract.
float evaluate_spot_shadow(
    uint lightIndex,
    float3 worldPosition,
    float3 worldNormal,
    float3 lightDirection)
{
    SpotShadowFrame shadowFrame = g_spotShadowFrames[0];
    bool hasShadow = false;
    [unroll]
    for (uint shadowIndex = 0; shadowIndex < k_maxSpotShadowCount;
         ++shadowIndex)
    {
        const SpotShadowFrame candidate = g_spotShadowFrames[shadowIndex];
        const uint shadowLightIndex = (uint)(candidate.params.w + 0.5f);
        if (candidate.params.x >= 0.5f && shadowLightIndex == lightIndex)
        {
            shadowFrame = candidate;
            hasShadow = true;
            break;
        }
    }

    if (!hasShadow)
    {
        return 1.0f;
    }

    const float4 shadowPosition =
        mul(mul(float4(worldPosition, 1.0f), shadowFrame.view),
            shadowFrame.projection);
    if (shadowPosition.w <= 0.0001f)
    {
        return 1.0f;
    }

    float3 ndc = shadowPosition.xyz / shadowPosition.w;
    ndc.z = ndc.z * 0.5f + 0.5f;
    const float2 tileUv =
        ndc.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
    const float2 uv = tileUv * shadowFrame.atlas.zw + shadowFrame.atlas.xy;
    if (tileUv.x < 0.0f || tileUv.x > 1.0f ||
        tileUv.y < 0.0f || tileUv.y > 1.0f ||
        ndc.z < 0.0f || ndc.z > 1.0f)
    {
        return 1.0f;
    }

    uint width = 1;
    uint height = 1;
    g_spotShadowMap.GetDimensions(width, height);
    const float2 texel = uv * float2(width, height);
    const int2 baseCoord = int2(texel);
    const float softness = max(shadowFrame.tuning.z, 0.0f);
    const int2 tileMinCoord =
        int2(shadowFrame.atlas.xy * float2(width, height));
    const int2 tileMaxCoord =
        int2((shadowFrame.atlas.xy + shadowFrame.atlas.zw) *
            float2(width, height)) - int2(1, 1);
    const float biasedDepth = ndc.z - shadow_receiver_bias(
        worldNormal,
        lightDirection,
        shadowFrame.params.y,
        shadowFrame.tuning.w,
        ndc.z);

    float visibility = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const int2 sampleCoord =
                int2(texel + float2((float)x, (float)y) * softness);
            const int2 coord = clamp(
                softness > 0.0f ? sampleCoord : baseCoord,
                tileMinCoord,
                tileMaxCoord);
            const float closestDepth = g_spotShadowMap.Load(int3(coord, 0));
            visibility += (biasedDepth <= closestDepth) ? 1.0f : 0.0f;
        }
    }

    const float rawVisibility = visibility / 9.0f;
    return lerp(1.0f, rawVisibility, saturate(shadowFrame.tuning.y));
}

// Directional shadow sampling uses the shared frame constants so cascade-free paths stay simple.
float evaluate_directional_shadow(
    uint lightIndex,
    float3 worldPosition,
    float3 worldNormal,
    float3 lightDirection)
{
    if (g_directionalShadowFrame.params.x < 0.5f)
    {
        return 1.0f;
    }

    const uint shadowLightIndex =
        (uint)(g_directionalShadowFrame.params.w + 0.5f);
    if (shadowLightIndex != lightIndex)
    {
        return 1.0f;
    }

    const float4 shadowPosition =
        mul(mul(float4(worldPosition, 1.0f), g_directionalShadowFrame.view),
            g_directionalShadowFrame.projection);
    if (shadowPosition.w <= 0.0001f)
    {
        return 1.0f;
    }

    const float3 ndc = shadowPosition.xyz / shadowPosition.w;
    const float2 uv = ndc.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
    if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f ||
        ndc.z < 0.0f || ndc.z > 1.0f)
    {
        return 1.0f;
    }

    uint width = 1;
    uint height = 1;
    g_directionalShadowMap.GetDimensions(width, height);
    const float2 texel = uv * float2(width, height);
    const int2 baseCoord = int2(texel);
    const float softness = max(g_directionalShadowFrame.tuning.z, 0.0f);
    const float biasedDepth = ndc.z - shadow_receiver_bias(
        worldNormal,
        -lightDirection,
        g_directionalShadowFrame.params.y,
        g_directionalShadowFrame.tuning.w,
        ndc.z);

    float visibility = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const int2 sampleCoord =
                int2(texel + float2((float)x, (float)y) * softness);
            const int2 coord = clamp(
                softness > 0.0f ? sampleCoord : baseCoord,
                int2(0, 0),
                int2((int)width - 1, (int)height - 1));
            const float closestDepth =
                g_directionalShadowMap.Load(int3(coord, 0));
            visibility += (biasedDepth <= closestDepth) ? 1.0f : 0.0f;
        }
    }

    const float rawVisibility = visibility / 9.0f;
    return lerp(
        1.0f,
        rawVisibility,
        saturate(g_directionalShadowFrame.tuning.y));
}

// Select the cube face in shader code so point-shadow sampling matches atlas face generation.
uint select_point_shadow_face(float3 fromLight)
{
    const float3 absDirection = abs(fromLight);
    if (absDirection.x >= absDirection.y && absDirection.x >= absDirection.z)
    {
        return fromLight.x >= 0.0f ? 0u : 1u;
    }
    if (absDirection.y >= absDirection.x && absDirection.y >= absDirection.z)
    {
        return fromLight.y >= 0.0f ? 2u : 3u;
    }
    return fromLight.z >= 0.0f ? 4u : 5u;
}

// Point shadow sampling maps the chosen cube face into atlas coordinates before depth comparison.
float evaluate_point_shadow(
    uint lightIndex,
    float3 worldPosition,
    float3 worldNormal,
    float3 lightDirection)
{
    const PointShadowFace firstFace = g_pointShadowFaces[0];
    if (firstFace.params.x < 0.5f)
    {
        return 1.0f;
    }

    const uint shadowLightIndex = (uint)(firstFace.params.w + 0.5f);
    if (shadowLightIndex != lightIndex)
    {
        return 1.0f;
    }

    const float3 lightPosition = firstFace.lightPositionRange.xyz;
    const float3 fromLight = worldPosition - lightPosition;
    const float distance = length(fromLight);
    if (distance > firstFace.lightPositionRange.w)
    {
        return 1.0f;
    }

    const uint faceIndex = select_point_shadow_face(fromLight);
    const PointShadowFace shadowFace = g_pointShadowFaces[faceIndex];
    const float4 shadowPosition =
        mul(mul(float4(worldPosition, 1.0f), shadowFace.view),
            shadowFace.projection);
    if (shadowPosition.w <= 0.0001f)
    {
        return 1.0f;
    }

    float3 ndc = shadowPosition.xyz / shadowPosition.w;
    ndc.z = ndc.z * 0.5f + 0.5f;
    const float2 tileUv =
        ndc.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
    const float2 uv = tileUv * shadowFace.atlas.zw + shadowFace.atlas.xy;
    if (tileUv.x < 0.0f || tileUv.x > 1.0f ||
        tileUv.y < 0.0f || tileUv.y > 1.0f ||
        ndc.z < 0.0f || ndc.z > 1.0f)
    {
        return 1.0f;
    }

    uint width = 1;
    uint height = 1;
    g_pointShadowMap.GetDimensions(width, height);
    const float2 texel = uv * float2(width, height);
    const int2 baseCoord = int2(texel);
    const float softness = max(shadowFace.tuning.z, 0.0f);
    const int2 tileMinCoord =
        int2(shadowFace.atlas.xy * float2(width, height));
    const int2 tileMaxCoord =
        int2((shadowFace.atlas.xy + shadowFace.atlas.zw) *
            float2(width, height)) - int2(1, 1);
    const float biasedDepth = ndc.z - shadow_receiver_bias(
        worldNormal,
        lightDirection,
        shadowFace.params.y,
        shadowFace.tuning.w,
        ndc.z);

    float visibility = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const int2 sampleCoord =
                int2(texel + float2((float)x, (float)y) * softness);
            const int2 coord = clamp(
                softness > 0.0f ? sampleCoord : baseCoord,
                tileMinCoord,
                tileMaxCoord);
            const float closestDepth = g_pointShadowMap.Load(int3(coord, 0));
            visibility += (biasedDepth <= closestDepth) ? 1.0f : 0.0f;
        }
    }

    const float rawVisibility = visibility / 9.0f;
    return lerp(1.0f, rawVisibility, saturate(shadowFace.tuning.y));
}

// Diffuse lighting is isolated so debug shading modes can reuse the same normal response.
float evaluate_diffuse(float3 worldNormal, float3 lightDirection)
{
    const float ndotl = dot(worldNormal, lightDirection);
    return pow(saturate(ndotl * 0.5f + 0.5f), 2.0f);
}

// Specular lighting is isolated to keep material shininess handling consistent per light type.
float evaluate_specular(
    float3 worldNormal,
    float3 lightDirection,
    float3 viewDirection,
    float shininess)
{
    const float3 halfVector = normalize(lightDirection + viewDirection);
    const float ndoth = saturate(dot(worldNormal, halfVector));
    return pow(ndoth, max(shininess, 1.0f));
}

// Spot falloff is kept as a helper so inner and outer cone tuning stays centralized.
float evaluate_spot_factor(float spotCos, float outerCos)
{
    const float innerCos = lerp(outerCos, 1.0f, 0.2f);
    const float factor =
        saturate((spotCos - outerCos) / max(innerCos - outerCos, 0.0001f));
    return factor * factor * (3.0f - 2.0f * factor);
}

// Lighting aggregation stays in one helper so debug modes can switch outputs without duplicating loops.
float3 evaluate_lighting(
    Material material,
    float3 worldPosition,
    float3 worldNormal,
    float3 viewDirection,
    bool receivesShadow)
{
    float3 lighting = g_lightFrame.ambientColorIntensity.rgb *
        g_lightFrame.ambientColorIntensity.a;

    const uint directionalCount = min(g_lightFrame.directionalLightCount, 4u);
    for (uint lightIndex = 0; lightIndex < directionalCount; ++lightIndex)
    {
        const DirectionalLight light = g_directionalLights[lightIndex];
        const float3 lightDirection = normalize(light.directionIntensity.xyz);
        const float3 surfaceToLight = -lightDirection;
        const float diffuse = evaluate_diffuse(worldNormal, surfaceToLight);
        const float specular = evaluate_specular(
            worldNormal,
            surfaceToLight,
            viewDirection,
            material.shininess);
        const float shadow = receivesShadow
            ? evaluate_directional_shadow(
                lightIndex,
                worldPosition,
                worldNormal,
                lightDirection)
            : 1.0f;
        lighting += light.color.rgb *
            light.directionIntensity.w *
            (diffuse + specular) *
            shadow;
    }

    const uint pointCount = min(g_lightFrame.pointLightCount, 64u);
    for (uint lightIndex = 0; lightIndex < pointCount; ++lightIndex)
    {
        const PointLight light = g_pointLights[lightIndex];
        const float3 toLight = light.positionRange.xyz - worldPosition;
        const float distance = length(toLight);
        const float range = max(light.positionRange.w, 0.001f);
        const float attenuation = saturate(1.0f - distance / range);
        const float3 lightDirection = distance > 0.0001f
            ? toLight / distance
            : float3(0.0f, 1.0f, 0.0f);
        const float diffuse = evaluate_diffuse(worldNormal, lightDirection);
        const float specular = evaluate_specular(
            worldNormal,
            lightDirection,
            viewDirection,
            material.shininess);
        const float shadow = receivesShadow
            ? evaluate_point_shadow(lightIndex, worldPosition, worldNormal,
                lightDirection)
            : 1.0f;
        lighting += light.colorIntensity.rgb *
            light.colorIntensity.w *
            (diffuse + specular) *
            attenuation *
            attenuation *
            shadow;
    }

    const uint spotCount = min(g_lightFrame.spotLightCount, 32u);
    for (uint lightIndex = 0; lightIndex < spotCount; ++lightIndex)
    {
        const SpotLight light = g_spotLights[lightIndex];
        const float3 toLight = light.positionRange.xyz - worldPosition;
        const float distance = length(toLight);
        const float range = max(light.positionRange.w, 0.001f);
        const float attenuation = saturate(1.0f - distance / range);
        const float3 lightDirection = distance > 0.0001f
            ? toLight / distance
            : float3(0.0f, 1.0f, 0.0f);
        const float3 spotDirection = normalize(light.directionOuterCos.xyz);
        const float spotCos = dot(-lightDirection, spotDirection);
        const float spotFactor =
            evaluate_spot_factor(spotCos, light.directionOuterCos.w);
        const float diffuse = evaluate_diffuse(worldNormal, lightDirection);
        const float specular = evaluate_specular(
            worldNormal,
            lightDirection,
            viewDirection,
            material.shininess);
        const float shadow = receivesShadow
            ? evaluate_spot_shadow(lightIndex, worldPosition, worldNormal,
                lightDirection)
            : 1.0f;
        lighting += light.colorIntensity.rgb *
            light.colorIntensity.w *
            (diffuse + specular) *
            attenuation *
            attenuation *
            spotFactor *
            shadow;
    }

    return lighting;
}

// Camera position is reconstructed from the view matrix to avoid an extra constant buffer field.
float3 camera_position()
{
    const float3 viewTranslation =
        float3(g_viewMatrix[3][0], g_viewMatrix[3][1], g_viewMatrix[3][2]);
    const float3x3 viewRotation = float3x3(
        g_viewMatrix[0][0], g_viewMatrix[0][1], g_viewMatrix[0][2],
        g_viewMatrix[1][0], g_viewMatrix[1][1], g_viewMatrix[1][2],
        g_viewMatrix[2][0], g_viewMatrix[2][1], g_viewMatrix[2][2]);
    return mul(-viewTranslation, transpose(viewRotation));
}

// Skybox reflection is optional so materials can opt in without branching on the CPU.
float3 evaluate_skybox_reflection(
    Material material,
    float3 worldPosition,
    float3 worldNormal)
{
    if (g_reflection.hasSkybox == 0 || material.useReflectionSkybox == 0)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    const float3 viewDirection = normalize(camera_position() - worldPosition);
    const float3 reflectionDirection = reflect(-viewDirection, worldNormal);
    const float reflectionSharpness = saturate(material.shininess / 256.0f);
    const float mipLevel = lerp(8.0f, 0.0f, reflectionSharpness);
    return g_reflectionSkybox.SampleLevel(
        g_sampler,
        reflectionDirection,
        mipLevel).rgb * reflectionSharpness;
}

// Pixel entry point resolves the pass output without changing upstream buffers.
float4 ps_main(VsOut input, bool isFrontFace : SV_IsFrontFace) : SV_Target0
{
    float3 worldNormal = normalize(input.worldNormal);
    if (!isFrontFace)
    {
        worldNormal = -worldNormal;
    }
    const uint shadingMode = g_debugViewShading.mode;
    const bool usesMaterial =
        shadingMode == k_debugViewShadingMaterial ||
        shadingMode == k_debugViewShadingMaterialLighting;
    const bool usesLighting =
        shadingMode == k_debugViewShadingLighting ||
        shadingMode == k_debugViewShadingMaterialLighting;

    float3 baseColor = k_solidColor;
    Material material;
    material.color = float4(1.0f, 1.0f, 1.0f, 1.0f);
    material.textureId = 0;
    material.useTexture = 0;
    material.useReflectionSkybox = 0;
    material.shininess = 32.0f;
    float4 textureColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    if (usesMaterial)
    {
        material = g_materials[input.materialId];
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
                g_textures[textureIndex].Load(int3(texelCoord, 0));
        }

        baseColor = material.color.rgb * textureColor.rgb;
    }

    const float alpha = usesMaterial
        ? saturate(material.color.a * textureColor.a)
        : 1.0f;

    float3 color = baseColor;
    if (usesLighting)
    {
        const float3 viewDirection =
            normalize(camera_position() - input.worldPosition);
        color *= max(
            evaluate_lighting(
                material,
                input.worldPosition,
                worldNormal,
                viewDirection,
                input.receivesShadow != 0u),
            0.0f);
        if (usesMaterial)
        {
            color += evaluate_skybox_reflection(
                material,
                input.worldPosition,
                worldNormal);
        }
    }
    return float4(color, alpha);
}
