// Effect sprite particle path.
// CPU simulation fills one ParticleSpriteGpu per live particle and the vertex
// shader expands each instance into a camera-facing quad.

struct ParticleSpriteGpu
{
    float4 positionSize;
    float4 color;
};

struct VsOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
};

cbuffer ViewProjectionBuffer : register(b0)
{
    row_major float4x4 g_view;
    row_major float4x4 g_projection;
    float4 g_cameraPosition;
};

StructuredBuffer<ParticleSpriteGpu> g_particles : register(t0);

static const float2 k_corners[6] =
{
    float2(-1.0f, -1.0f),
    float2(-1.0f, 1.0f),
    float2(1.0f, 1.0f),
    float2(-1.0f, -1.0f),
    float2(1.0f, 1.0f),
    float2(1.0f, -1.0f),
};

VsOutput vs_main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const ParticleSpriteGpu particle = g_particles[instanceId];
    float4 viewPosition = mul(float4(particle.positionSize.xyz, 1.0f), g_view);
    viewPosition.xy += k_corners[vertexId] * particle.positionSize.w;

    VsOutput output;
    output.position = mul(viewPosition, g_projection);
    output.color = particle.color;
    return output;
}

float4 ps_main(VsOutput input) : SV_Target0
{
    return saturate(input.color);
}
