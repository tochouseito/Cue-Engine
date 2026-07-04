// Effect viewer grid path.
// Grid lines are generated procedurally on the XZ plane.

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

static const uint kLineCountPerAxis = 41;
static const float kHalfExtent = 10.0f;
static const float kSpacing = 0.5f;

VsOutput vs_main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const bool isZLine = instanceId >= kLineCountPerAxis;
    const uint lineIndex = isZLine ? instanceId - kLineCountPerAxis : instanceId;
    const int signedIndex = int(lineIndex) - int(kLineCountPerAxis / 2);
    const float offset = float(signedIndex) * kSpacing;
    const float lineEnd = vertexId == 0 ? -kHalfExtent : kHalfExtent;

    float3 worldPosition = float3(0.0f, 0.0f, 0.0f);
    if (isZLine)
    {
        worldPosition = float3(lineEnd, 0.0f, offset);
    }
    else
    {
        worldPosition = float3(offset, 0.0f, lineEnd);
    }

    const bool isAxis = signedIndex == 0;
    const bool isMajor = signedIndex % 4 == 0;
    float4 color = isMajor ? float4(0.32f, 0.34f, 0.36f, 0.72f) : float4(0.20f, 0.22f, 0.24f, 0.48f);
    if (isAxis)
    {
        color = isZLine ? float4(0.72f, 0.18f, 0.16f, 0.86f) : float4(0.16f, 0.38f, 0.78f, 0.86f);
    }

    VsOutput output;
    output.position = mul(mul(float4(worldPosition, 1.0f), g_view), g_projection);
    output.color = color;
    return output;
}

float4 ps_main(VsOutput input) : SV_Target0
{
    return input.color;
}
