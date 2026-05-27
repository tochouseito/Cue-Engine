#include "ParticleCommon.hlsli"

cbuffer ViewProjection : register(b0)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
}

ConstantBuffer<ParticleFrame> g_frame : register(b1);

StructuredBuffer<Particle> g_particles : register(t0);
StructuredBuffer<ParticleTrailPoint> g_trails : register(t1);
Texture2D<float4> g_textures[] : register(t0, space1);

struct VsOut
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
    nointerpolation uint textureId : TEXCOORD1;
    nointerpolation uint useTexture : TEXCOORD2;
};

static const float2 k_stripCorners[6] =
{
    float2(0.0f, -1.0f),
    float2(1.0f, -1.0f),
    float2(0.0f, 1.0f),
    float2(0.0f, 1.0f),
    float2(1.0f, -1.0f),
    float2(1.0f, 1.0f),
};

VsOut make_empty_output()
{
    VsOut output = (VsOut)0;
    output.position = float4(0.0f, 0.0f, 0.0f, 1.0f);
    output.color = float4(0.0f, 0.0f, 0.0f, 0.0f);
    return output;
}

VsOut vs_main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const Particle particle = g_particles[instanceId];
    if (particle.isAlive == 0u ||
        (particle.rendererType != 1u && particle.rendererType != 2u))
    {
        return make_empty_output();
    }

    const uint segmentIndex = vertexId / 6u;
    const uint localVertexId = vertexId % 6u;
    const uint segmentCount = min(
        max(particle.trailSegmentCount, 1u),
        max(g_frame.maxTrailSegmentCount, 1u));
    if (segmentIndex >= segmentCount)
    {
        return make_empty_output();
    }

    const float lifeRate = particle.lifetime > 0.0f
        ? saturate(particle.age / particle.lifetime)
        : 0.0f;
    const float4 color = evaluate_curve4(
        particle.startColor,
        particle.midColor,
        particle.endColor,
        lifeRate,
        particle.curveMidTime);

    const float trailWidth = max(particle.trailWidth, 0.001f);
    const uint maxSegmentCount = max(g_frame.maxTrailSegmentCount, 1u);
    const uint baseIndex = instanceId * maxSegmentCount;
    const uint headHistoryIndex =
        (g_frame.trailFrameIndex + maxSegmentCount - segmentIndex) %
        maxSegmentCount;
    const uint tailHistoryIndex =
        (g_frame.trailFrameIndex + maxSegmentCount - segmentIndex - 1u) %
        maxSegmentCount;
    ParticleTrailPoint headPoint = g_trails[baseIndex + headHistoryIndex];
    ParticleTrailPoint tailPoint = g_trails[baseIndex + tailHistoryIndex];
    if (segmentIndex == 0u)
    {
        headPoint.position = particle.position;
    }
    const float fallbackDistance = particle.trailLength /
        (float)max(segmentCount, 1u);
    if (dot(headPoint.position - tailPoint.position, headPoint.position - tailPoint.position) <= 0.000001f)
    {
        const float3 velocityDirection = dot(particle.velocity, particle.velocity) > 0.0001f
            ? normalize(particle.velocity)
            : float3(0.0f, 1.0f, 0.0f);
        tailPoint.position = headPoint.position - velocityDirection * fallbackDistance;
    }

    float4 headView = mul(float4(headPoint.position, 1.0f), g_viewMatrix);
    float4 tailView = mul(float4(tailPoint.position, 1.0f), g_viewMatrix);
    const float2 segment = headView.xy - tailView.xy;
    const float2 side = dot(segment, segment) > 0.000001f
        ? normalize(float2(-segment.y, segment.x))
        : float2(1.0f, 0.0f);

    const float2 corner = k_stripCorners[localVertexId];
    float4 viewPosition = lerp(headView, tailView, corner.x);
    viewPosition.xy += side * corner.y * trailWidth * 0.5f;

    VsOut output;
    output.position = mul(viewPosition, g_projectionMatrix);
    output.texcoord = float2(corner.x, corner.y * 0.5f + 0.5f);
    output.color = color;
    output.textureId = particle.textureId;
    output.useTexture = particle.useTexture;
    return output;
}

float4 ps_main(VsOut input) : SV_Target0
{
    float4 color = input.color;
    if (input.useTexture != 0u)
    {
        const uint textureIndex = NonUniformResourceIndex(input.textureId);
        uint textureWidth = 1;
        uint textureHeight = 1;
        g_textures[textureIndex].GetDimensions(textureWidth, textureHeight);

        const float2 uv = saturate(input.texcoord);
        const uint2 texelCoord = uint2(
            min((uint)(uv.x * textureWidth), textureWidth - 1),
            min((uint)(uv.y * textureHeight), textureHeight - 1));
        color *= g_textures[textureIndex].Load(int3(texelCoord, 0));
    }

    if (color.a <= 0.0f)
    {
        discard;
    }

    return color;
}
