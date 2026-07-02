Texture2D<float> g_depth : register(t0);
RWTexture2D<uint> g_outHiZ : register(u0);

cbuffer SourceWidthParam : register(b0)
{
    uint g_sourceWidth;
};

cbuffer SourceHeightParam : register(b1)
{
    uint g_sourceHeight;
};

cbuffer OutputWidthParam : register(b2)
{
    uint g_outputWidth;
};

cbuffer OutputHeightParam : register(b3)
{
    uint g_outputHeight;
};

uint encode_depth(float depth)
{
    return (uint)round(saturate(depth) * 4294967295.0f);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 dst = dispatchThreadId.xy;
    if (dst.x >= g_outputWidth || dst.y >= g_outputHeight)
    {
        return;
    }

    const uint2 srcBegin =
        uint2((dst.x * g_sourceWidth) / g_outputWidth,
              (dst.y * g_sourceHeight) / g_outputHeight);
    const uint2 srcEnd =
        uint2(((dst.x + 1u) * g_sourceWidth + g_outputWidth - 1u) /
                  g_outputWidth,
              ((dst.y + 1u) * g_sourceHeight + g_outputHeight - 1u) /
                  g_outputHeight);

    float minDepth = 1.0f;
    [loop]
    for (uint y = srcBegin.y; y < min(srcEnd.y, g_sourceHeight); ++y)
    {
        [loop]
        for (uint x = srcBegin.x; x < min(srcEnd.x, g_sourceWidth); ++x)
        {
            minDepth = min(minDepth, g_depth.Load(int3(x, y, 0)));
        }
    }

    g_outHiZ[dst] = encode_depth(minDepth);
}
