// Compute visibility buffer resolve.

#include "VisibilityResolveCommon.hlsli"

RWTexture2D<float4> g_outputColor : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= g_screenWidth ||
        dispatchThreadId.y >= g_screenHeight)
    {
        return;
    }

    const float2 pixelPosition = float2(dispatchThreadId.xy) + 0.5f;
    const VisibilityResolvePayload payload =
        visibility_resolve_sample(pixelPosition);

    float4 color = float4(0.0f, 0.0f, 0.0f, 1.0f);
    if (payload.status == kVisibilityResolveInvalid)
    {
        color = float4(1.0f, 0.0f, 1.0f, 1.0f);
    }
    else if (payload.status == kVisibilityResolveHit)
    {
        color = visibility_resolve_lit(payload, pixelPosition);
    }

    g_outputColor[dispatchThreadId.xy] = color;
}
