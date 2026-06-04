cbuffer DepthSizeParam : register(b0)
{
    uint g_depthWidth;
};

cbuffer DepthHeightParam : register(b1)
{
    uint g_depthHeight;
};

cbuffer TileCountXParam : register(b2)
{
    uint g_tileCountX;
};

cbuffer TileCountYParam : register(b3)
{
    uint g_tileCountY;
};

cbuffer TileSizeParam : register(b4)
{
    uint g_tileSize;
};

cbuffer DepthScaleParam : register(b5)
{
    uint g_depthScale;
};

Texture2D<float> g_sceneDepth : register(t0);
RWByteAddressBuffer g_hizDepth : register(u0);

uint quantize_depth(float depth)
{
    return (uint)min(max(depth, 0.0f) * 4294967295.0f, 4294967295.0f);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint tileX = dispatchThreadId.x;
    const uint tileY = dispatchThreadId.y;
    if (tileX >= g_tileCountX || tileY >= g_tileCountY)
    {
        return;
    }

    const uint depthScale = max(g_depthScale, 1u);
    const uint2 fullPixelStart = uint2(tileX, tileY) * g_tileSize;
    const uint2 fullPixelEnd = fullPixelStart + g_tileSize;
    const uint2 pixelStart = fullPixelStart / depthScale;
    const uint2 pixelEnd = min(
        (fullPixelEnd + depthScale - 1u) / depthScale,
        uint2(g_depthWidth, g_depthHeight));

    float maxDepth = 0.0f;
    for (uint y = pixelStart.y; y < pixelEnd.y; ++y)
    {
        for (uint x = pixelStart.x; x < pixelEnd.x; ++x)
        {
            maxDepth = max(maxDepth, g_sceneDepth.Load(int3(x, y, 0)));
        }
    }

    const uint tileIndex = tileY * g_tileCountX + tileX;
    g_hizDepth.Store(tileIndex * 4u, quantize_depth(maxDepth));
}
