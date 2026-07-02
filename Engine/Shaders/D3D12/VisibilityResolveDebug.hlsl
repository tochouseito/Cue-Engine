// Visibility buffer debug resolve.

#include "VisibilityResolveCommon.hlsli"

cbuffer DebugModeParam : register(b3)
{
    uint g_debugMode;
};

VisibilityResolveVsOutput vs_main(uint vertexId : SV_VertexID)
{
    return visibility_resolve_vs_main(vertexId);
}

float4 ps_main(VisibilityResolveVsOutput input) : SV_Target0
{
    const VisibilityResolvePayload payload =
        visibility_resolve_sample(input.position.xy);
    if (payload.status == kVisibilityResolveBackground)
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }
    if (payload.status == kVisibilityResolveInvalid)
    {
        return float4(1.0f, 0.0f, 1.0f, 1.0f);
    }

    if (g_debugMode == 4u)
    {
        return float4(payload.worldNormal * 0.5f + 0.5f, 1.0f);
    }

    if (g_debugMode == 5u)
    {
        return float4(frac(payload.uv), 0.0f, 1.0f);
    }

    if (g_debugMode == 6u)
    {
        return visibility_resolve_lit(payload, input.position.xy);
    }

    if (g_debugMode == 7u)
    {
        return payload.baseColor;
    }

    return float4(saturate(payload.barycentric), 1.0f);
}
