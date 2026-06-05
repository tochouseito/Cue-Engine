// Clustered forward lighting 用の view-space cluster grid を構築する pass。
// 画面を固定 X/Y grid と logarithmic depth slice に分割し、各 cluster の
// AABB を ClusterLightCulling に渡す。

struct Cluster
{
    // 1 cluster の view-space bounds。
    // ClusterLightCulling はこの AABB と light sphere を交差判定する。
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

cbuffer ClusterCountXParam : register(b3)
{
    uint g_clusterCountX;
};

cbuffer ClusterCountYParam : register(b4)
{
    uint g_clusterCountY;
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
    // screen pixel 座標と view-space z から view-space の点へ戻す。
    // frustum slice の 8 corner を作るために使う。
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

float slice_to_view_z(uint sliceIndex, float nearZ, float farZ)
{
    // depth slice は logarithmic。近距離の cluster 密度を上げ、
    // 遠距離で過剰に細かくならないようにする。
    const float safeNearZ = max(nearZ, 0.0001f);
    const float safeFarZ = max(farZ, safeNearZ + 0.0001f);
    const float t =
        (float)sliceIndex / max((float)g_depthSliceCount, 1.0f);
    return safeNearZ * pow(safeFarZ / safeNearZ, t);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint tileX = dispatchThreadId.x;
    const uint tileY = dispatchThreadId.y;
    const uint sliceZ = dispatchThreadId.z;
    if (tileX >= g_clusterCountX || tileY >= g_clusterCountY ||
        sliceZ >= g_depthSliceCount)
    {
        return;
    }

    float nearZ = 0.0f;
    float farZ = 0.0f;
    projection_near_far(nearZ, farZ);

    const float z0 = slice_to_view_z(sliceZ, nearZ, farZ);
    const float z1 = slice_to_view_z(sliceZ + 1u, nearZ, farZ);

    // 固定 cluster count 方式なので、画面サイズを 16x9 などの
    // cluster grid に割り当てて screen-space tile 範囲を作る。
    const float2 minScreen =
        float2((float)g_screenWidth * (float)tileX /
                   max((float)g_clusterCountX, 1.0f),
               (float)g_screenHeight * (float)tileY /
                   max((float)g_clusterCountY, 1.0f));
    const float2 maxScreen =
        float2((float)g_screenWidth * (float)(tileX + 1u) /
                   max((float)g_clusterCountX, 1.0f),
               (float)g_screenHeight * (float)(tileY + 1u) /
                   max((float)g_clusterCountY, 1.0f));

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

    // screen tile と depth slice から得た frustum の 8 点を包む AABB を作る。
    // sphere/AABB の交差判定に落とすことで LightCulling を単純にする。
    float3 minPoint = corners[0];
    float3 maxPoint = corners[0];
    [unroll]
    for (uint cornerIndex = 1u; cornerIndex < 8u; ++cornerIndex)
    {
        minPoint = min(minPoint, corners[cornerIndex]);
        maxPoint = max(maxPoint, corners[cornerIndex]);
    }

    const uint clusterIndex =
        (sliceZ * g_clusterCountY + tileY) * g_clusterCountX + tileX;
    Cluster cluster;
    cluster.minPoint = float4(minPoint, 0.0f);
    cluster.maxPoint = float4(maxPoint, 0.0f);
    g_clusters[clusterIndex] = cluster;
}
