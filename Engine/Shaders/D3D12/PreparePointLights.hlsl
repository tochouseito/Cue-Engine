// Clustered lighting の前処理。
// PointLightBuffer の world-space light を view-space へ一度だけ変換し、
// ClusterLightCulling が cluster 数ぶん matrix multiply しないようにする。

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

struct ViewPointLight
{
    float4 positionRadius;
    float4 colorIntensity;
    uint sourceIndex;
    uint padding0;
    uint padding1;
    uint padding2;
};

cbuffer ViewProjection : register(b0)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
};

ConstantBuffer<LightFrame> g_lightFrame : register(b2);

cbuffer MaxPointLightCountParam : register(b3)
{
    uint g_maxPointLightCount;
};

StructuredBuffer<PointLight> g_pointLights : register(t0);
RWStructuredBuffer<ViewPointLight> g_viewPointLights : register(u0);

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint lightIndex = dispatchThreadId.x;

    // Dispatch は 64 thread 単位に丸められるため、capacity 外の余り
    // thread は必ずここで止める。これを省くと UAV 範囲外書き込みになる。
    if (lightIndex >= g_maxPointLightCount)
    {
        return;
    }

    // ClusterLightCulling は capacity 分を読むため、未使用 slot には
    // どの cluster とも交差しない light を入れておく。
    if (lightIndex >= g_lightFrame.pointLightCount)
    {
        ViewPointLight emptyLight;
        emptyLight.positionRadius = float4(1.0e20f, 1.0e20f, 1.0e20f, 0.0f);
        emptyLight.colorIntensity = float4(0.0f, 0.0f, 0.0f, 0.0f);
        emptyLight.sourceIndex = 0u;
        emptyLight.padding0 = 0u;
        emptyLight.padding1 = 0u;
        emptyLight.padding2 = 0u;
        g_viewPointLights[lightIndex] = emptyLight;
        return;
    }

    const PointLight light = g_pointLights[lightIndex];

    // world-space light を一度だけ view-space へ変換する。
    // これにより clusterCount * lightCount 回の matrix multiply を避ける。
    const float3 viewPosition =
        mul(float4(light.positionRange.xyz, 1.0f), g_viewMatrix).xyz;

    ViewPointLight outputLight;
    outputLight.positionRadius = float4(viewPosition, light.positionRange.w);
    outputLight.colorIntensity = light.colorIntensity;
    outputLight.sourceIndex = lightIndex;
    outputLight.padding0 = 0u;
    outputLight.padding1 = 0u;
    outputLight.padding2 = 0u;
    g_viewPointLights[lightIndex] = outputLight;
}
