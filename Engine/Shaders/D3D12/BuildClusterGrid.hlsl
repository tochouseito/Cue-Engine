struct Cluster
{
    float4 minPoint;
    float4 maxPoint;
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

cbuffer TileCountXParam : register(b3)
{
    uint g_tileCountX;
};

cbuffer TileCountYParam : register(b4)
{
    uint g_tileCountY;
};

cbuffer DepthSliceCountParam : register(b5)
{
    uint g_depthSliceCount;
};

RWStructuredBuffer<Cluster> g_clusters : register(u0);

float2 projection_scale()
{
    return max(abs(float2(g_projectionMatrix[0][0], g_projectionMatrix[1][1])),
               float2(0.0001f, 0.0001f));
}

void projection_near_far(out float nearZ, out float farZ)
{
    const float a = g_projectionMatrix[2][2];
    const float b = g_projectionMatrix[3][2];
    nearZ = max(b / (-1.0f - a), 0.0001f);
    farZ = max(b / (1.0f - a), nearZ + 0.0001f);
}

float3 screen_to_view(float2 screenPosition, float viewZ)
{
    const float2 scale = projection_scale();
    const float2 screenSize =
        max(float2(g_screenWidth, g_screenHeight), float2(1.0f, 1.0f));
    const float2 ndc =
        float2(screenPosition.x / screenSize.x * 2.0f - 1.0f,
               1.0f - screenPosition.y / screenSize.y * 2.0f);
    return float3(ndc.x * viewZ / scale.x,
                  ndc.y * viewZ / scale.y,
                  viewZ);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint tileX = dispatchThreadId.x;
    const uint tileY = dispatchThreadId.y;
    const uint sliceZ = dispatchThreadId.z;
    if (tileX >= g_tileCountX || tileY >= g_tileCountY ||
        sliceZ >= g_depthSliceCount)
    {
        return;
    }

    float nearZ = 0.0f;
    float farZ = 0.0f;
    projection_near_far(nearZ, farZ);

    const float slice0 =
        (float)sliceZ / max((float)g_depthSliceCount, 1.0f);
    const float slice1 =
        (float)(sliceZ + 1u) / max((float)g_depthSliceCount, 1.0f);
    const float z0 = lerp(nearZ, farZ, slice0);
    const float z1 = lerp(nearZ, farZ, slice1);

    const float2 minScreen =
        float2((float)(tileX * 32u), (float)(tileY * 32u));
    const float2 maxScreen =
        min(minScreen + float2(32.0f, 32.0f),
            float2((float)g_screenWidth, (float)g_screenHeight));

    const float3 corners[8] =
    {
        screen_to_view(float2(minScreen.x, minScreen.y), z0),
        screen_to_view(float2(maxScreen.x, minScreen.y), z0),
        screen_to_view(float2(minScreen.x, maxScreen.y), z0),
        screen_to_view(float2(maxScreen.x, maxScreen.y), z0),
        screen_to_view(float2(minScreen.x, minScreen.y), z1),
        screen_to_view(float2(maxScreen.x, minScreen.y), z1),
        screen_to_view(float2(minScreen.x, maxScreen.y), z1),
        screen_to_view(float2(maxScreen.x, maxScreen.y), z1),
    };

    float3 minPoint = corners[0];
    float3 maxPoint = corners[0];
    [unroll]
    for (uint cornerIndex = 1u; cornerIndex < 8u; ++cornerIndex)
    {
        minPoint = min(minPoint, corners[cornerIndex]);
        maxPoint = max(maxPoint, corners[cornerIndex]);
    }

    const uint clusterIndex =
        (sliceZ * g_tileCountY + tileY) * g_tileCountX + tileX;
    Cluster cluster;
    cluster.minPoint = float4(minPoint, 0.0f);
    cluster.maxPoint = float4(maxPoint, 0.0f);
    g_clusters[clusterIndex] = cluster;
}
