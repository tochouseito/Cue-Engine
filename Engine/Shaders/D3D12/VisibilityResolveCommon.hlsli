#ifndef CUE_VISIBILITY_RESOLVE_COMMON_HLSLI
#define CUE_VISIBILITY_RESOLVE_COMMON_HLSLI

#include "ClusteredForwardLighting.hlsli"

Texture2D<uint2> g_visibility : register(t0);

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

struct MeshRange
{
    uint indexCount;
    uint startIndex;
    int baseVertex;
    uint firstMeshlet;
    uint meshletCount;
    uint rangeStartIndex;
    uint rangeIndexCount;
    uint visibilityTriangleStart;
};

struct VisibilityTriangle
{
    float4 position0;
    float4 position1;
    float4 position2;
    float4 normal0;
    float4 normal1;
    float4 normal2;
    float4 uv01;
    float4 uv2;
};

cbuffer ViewProjection : register(b0)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
};

cbuffer ScreenWidthParam : register(b1)
{
    uint g_screenWidth;
};

cbuffer ScreenHeightParam : register(b2)
{
    uint g_screenHeight;
};

ConstantBuffer<LightFrame> g_lightFrame : register(b4);

cbuffer ClusterCountXParam : register(b5)
{
    uint g_clusterCountX;
};

cbuffer ClusterCountYParam : register(b6)
{
    uint g_clusterCountY;
};

cbuffer ClusterDepthSliceCountParam : register(b7)
{
    uint g_clusterDepthSliceCount;
};

cbuffer ClusterNearZParam : register(b8)
{
    float g_clusterNearZ;
};

cbuffer ClusterFarZParam : register(b9)
{
    float g_clusterFarZ;
};

cbuffer ClusterInvLogFarNearParam : register(b10)
{
    float g_clusterInvLogFarNear;
};

StructuredBuffer<RenderObject> g_renderObjects : register(t1);
StructuredBuffer<MeshRange> g_meshRanges : register(t2);
StructuredBuffer<Transform> g_transforms : register(t5);
StructuredBuffer<Material> g_materials : register(t8);
StructuredBuffer<DirectionalLight> g_directionalLights : register(t9);
StructuredBuffer<PointLight> g_pointLights : register(t10);
StructuredBuffer<ClusterLightRange> g_clusterLightRanges : register(t11);
StructuredBuffer<uint> g_clusterLightIndices : register(t12);
StructuredBuffer<VisibilityTriangle> g_visibilityTriangles : register(t13);

#define CUE_CLUSTERED_FORWARD_LIGHTING_IMPLEMENTATION
#include "ClusteredForwardLighting.hlsli"

struct VisibilityResolveVsOutput
{
    float4 position : SV_POSITION;
};

struct VisibilityResolvePayload
{
    uint status;
    float3 barycentric;
    float3 perspectiveWeights;
    float3 worldPosition;
    float3 viewPosition;
    float3 worldNormal;
    float2 uv;
    Material material;
};

static const uint kVisibilityResolveBackground = 0u;
static const uint kVisibilityResolveInvalid = 1u;
static const uint kVisibilityResolveHit = 2u;

VisibilityResolveVsOutput visibility_resolve_vs_main(uint vertexId)
{
    const float2 positions[3] =
    {
        float2(-1.0f, -1.0f),
        float2(-1.0f, 3.0f),
        float2(3.0f, -1.0f)
    };

    VisibilityResolveVsOutput output;
    output.position = float4(positions[vertexId], 0.0f, 1.0f);
    return output;
}

float2 visibility_clip_to_pixel(float4 clipPosition)
{
    const float invW = rcp(max(abs(clipPosition.w), 0.000001f));
    const float2 ndc = clipPosition.xy * invW;
    return float2(
        (ndc.x * 0.5f + 0.5f) * (float)g_screenWidth,
        (0.5f - ndc.y * 0.5f) * (float)g_screenHeight);
}

float visibility_edge(float2 a, float2 b, float2 p)
{
    return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
}

float3 visibility_barycentric(float2 p, float2 a, float2 b, float2 c)
{
    const float area = visibility_edge(a, b, c);
    if (abs(area) < 0.000001f)
    {
        return float3(1.0f, 0.0f, 1.0f);
    }

    const float w0 = visibility_edge(b, c, p) / area;
    const float w1 = visibility_edge(c, a, p) / area;
    const float w2 = visibility_edge(a, b, p) / area;
    return float3(w0, w1, w2);
}

float3 visibility_perspective_correct_weights(float3 weights, float4 c0,
                                              float4 c1, float4 c2)
{
    const float invW0 = rcp(max(abs(c0.w), 0.000001f));
    const float invW1 = rcp(max(abs(c1.w), 0.000001f));
    const float invW2 = rcp(max(abs(c2.w), 0.000001f));
    const float3 corrected = weights * float3(invW0, invW1, invW2);
    const float sumWeights =
        max(corrected.x + corrected.y + corrected.z, 0.000001f);
    return corrected / sumWeights;
}

VisibilityResolvePayload visibility_resolve_sample(float2 pixelPosition)
{
    VisibilityResolvePayload payload;
    payload.status = kVisibilityResolveBackground;
    payload.barycentric = float3(0.0f, 0.0f, 0.0f);
    payload.perspectiveWeights = float3(0.0f, 0.0f, 0.0f);
    payload.worldPosition = float3(0.0f, 0.0f, 0.0f);
    payload.viewPosition = float3(0.0f, 0.0f, 0.0f);
    payload.worldNormal = float3(0.0f, 0.0f, 1.0f);
    payload.uv = float2(0.0f, 0.0f);
    payload.material.color = float4(1.0f, 0.0f, 1.0f, 1.0f);
    payload.material.textureId = 0u;
    payload.material.useTexture = 0u;
    payload.material.useReflectionSkybox = 0u;
    payload.material.shininess = 32.0f;

    const uint2 pixel = uint2(pixelPosition);
    const uint2 id = g_visibility.Load(int3(pixel, 0));
    if (id.x == 0u)
    {
        return payload;
    }

    const uint renderObjectIndex = id.x - 1u;
    const RenderObject renderObject = g_renderObjects[renderObjectIndex];
    const MeshRange meshRange = g_meshRanges[renderObject.meshId];
    if (id.y * 3u + 2u >= meshRange.indexCount)
    {
        payload.status = kVisibilityResolveInvalid;
        return payload;
    }

    const VisibilityTriangle tri =
        g_visibilityTriangles[meshRange.visibilityTriangleStart + id.y];
    const Transform transform = g_transforms[renderObject.transformId];

    float4 w0;
    float4 w1;
    float4 w2;
    float4 v0;
    float4 v1;
    float4 v2;

    if ((renderObject.drawFlags & 1u) != 0u)
    {
        const float4 worldCenter =
            float4(renderObject.boundsCenterRadius.xyz, 1.0f);
        const float objectScale = length(transform.worldMatrix[0].xyz);
        const float4 viewCenter = mul(worldCenter, g_viewMatrix);

        v0 = viewCenter;
        v1 = viewCenter;
        v2 = viewCenter;
        v0.xy += tri.position0.xy * objectScale;
        v1.xy += tri.position1.xy * objectScale;
        v2.xy += tri.position2.xy * objectScale;

        w0 = worldCenter;
        w1 = worldCenter;
        w2 = worldCenter;
    }
    else
    {
        w0 = mul(tri.position0, transform.worldMatrix);
        w1 = mul(tri.position1, transform.worldMatrix);
        w2 = mul(tri.position2, transform.worldMatrix);
        v0 = mul(w0, g_viewMatrix);
        v1 = mul(w1, g_viewMatrix);
        v2 = mul(w2, g_viewMatrix);
    }

    const float4 c0 = mul(v0, g_projectionMatrix);
    const float4 c1 = mul(v1, g_projectionMatrix);
    const float4 c2 = mul(v2, g_projectionMatrix);
    const float2 p0 = visibility_clip_to_pixel(c0);
    const float2 p1 = visibility_clip_to_pixel(c1);
    const float2 p2 = visibility_clip_to_pixel(c2);
    const float3 b = visibility_barycentric(pixelPosition, p0, p1, p2);
    const float3 pc = visibility_perspective_correct_weights(b, c0, c1, c2);

    float3 normal = float3(0.0f, 0.0f, 1.0f);
    if ((renderObject.drawFlags & 1u) == 0u)
    {
        const float3 n0 = mul(tri.normal0, transform.normalMatrix).xyz;
        const float3 n1 = mul(tri.normal1, transform.normalMatrix).xyz;
        const float3 n2 = mul(tri.normal2, transform.normalMatrix).xyz;
        normal = normalize(n0 * pc.x + n1 * pc.y + n2 * pc.z);
    }

    payload.status = kVisibilityResolveHit;
    payload.barycentric = b;
    payload.perspectiveWeights = pc;
    payload.worldPosition = w0.xyz * pc.x + w1.xyz * pc.y + w2.xyz * pc.z;
    payload.viewPosition = v0.xyz * pc.x + v1.xyz * pc.y + v2.xyz * pc.z;
    payload.worldNormal = normal;
    payload.uv = tri.uv01.xy * pc.x + tri.uv01.zw * pc.y +
                 tri.uv2.xy * pc.z;
    payload.material = g_materials[renderObject.materialId];
    return payload;
}

float4 visibility_resolve_lit(VisibilityResolvePayload payload,
                              float2 pixelPosition)
{
    const float3 lighting =
        max(cue_evaluate_clustered_lighting(pixelPosition,
                                            payload.worldPosition,
                                            payload.viewPosition,
                                            payload.worldNormal),
            0.0f);
    return float4(payload.material.color.rgb * lighting,
                  payload.material.color.a);
}

#endif
