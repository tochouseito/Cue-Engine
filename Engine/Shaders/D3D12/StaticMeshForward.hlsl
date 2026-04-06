#include "DrawCommon.hlsli"

struct VsOut
{
    float4 position : SV_POSITION;
    float3 worldNormal : NORMAL0;
    float2 texcoord : TEXCOORD0;
};

static const float k_pi = 3.14159265359f;
static const float k_aspectRatio = 16.0f / 9.0f;
static const float k_nearClip = 0.1f;
static const float k_farClip = 100.0f;

StructuredBuffer<RenderObject> g_renderObjects : register(t0);
StructuredBuffer<Transform> g_transforms : register(t1);
StructuredBuffer<float4> g_positions : register(t2);
StructuredBuffer<float2> g_uvs : register(t3);
StructuredBuffer<float3> g_normals : register(t4);

float4x4 make_view_matrix()
{
    return float4x4(
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 6.0f, 1.0f);
}

float4x4 make_projection_matrix()
{
    const float fovY = 60.0f * (k_pi / 180.0f);
    const float f = tan(fovY * 0.5f);

    return float4x4(
        1.0f / (k_aspectRatio * f), 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f / f, 0.0f, 0.0f,
        0.0f, 0.0f, (k_farClip + k_nearClip) / (k_farClip - k_nearClip), 1.0f,
        0.0f, 0.0f, -(2.0f * k_farClip * k_nearClip) / (k_farClip - k_nearClip), 0.0f);
}

VsOut vs_main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const RenderObject renderObject = g_renderObjects[instanceId];
    const Transform transform = g_transforms[renderObject.transformId];

    const float4 localPosition = g_positions[vertexId];
    const float2 localUv = g_uvs[vertexId];
    const float3 localNormal = g_normals[vertexId];

    const float4 worldPosition = mul(localPosition, transform.worldMatrix);
    const float3 worldNormal = normalize(mul(float4(localNormal, 0.0f), transform.worldMatrix).xyz);

    const float4x4 viewMatrix = make_view_matrix();
    const float4x4 projectionMatrix = make_projection_matrix();

    VsOut output;
    output.position = mul(mul(worldPosition, viewMatrix), projectionMatrix);
    output.worldNormal = worldNormal;
    output.texcoord = localUv;
    return output;
}

float4 ps_main(VsOut input) : SV_Target0
{
    const float3 lightDirection = normalize(float3(-0.4f, -0.7f, -0.6f));
    const float ndotl = saturate(dot(normalize(input.worldNormal), -lightDirection));
    const float3 baseColor = float3(input.texcoord, 1.0f - input.texcoord.x * 0.5f);
    const float3 color = baseColor * (0.2f + ndotl * 0.8f);
    return float4(color, 1.0f);
}
