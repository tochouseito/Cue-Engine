// Production visibility buffer resolve.

#include "VisibilityResolveCommon.hlsli"

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

    return visibility_resolve_lit(payload, input.position.xy);
}
