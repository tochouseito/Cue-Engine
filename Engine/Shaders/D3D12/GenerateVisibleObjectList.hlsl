struct RenderableInfo
{
    uint objectId;
    uint visible;
    uint meshId;
    uint transformId;
    uint materialId;
    uint castsShadow;
    uint receivesShadow;
    uint shadowCasterMode;
    uint skinPaletteOffset;
    uint skinPaletteCount;
    float4 boundsCenterRadius;
};

struct RenderObject
{
    uint objectId;
    uint meshId;
    uint transformId;
    uint materialId;
    uint castsShadow;
    uint receivesShadow;
    uint shadowCasterMode;
    uint skinPaletteOffset;
    uint skinPaletteCount;
};

cbuffer DispatchParam : register(b0)
{
    uint g_objectCount;
};

cbuffer ViewProjection : register(b1)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
};

StructuredBuffer<RenderableInfo> g_renderableInfos : register(t0);
RWStructuredBuffer<RenderObject> g_renderObjects : register(u0);
RWByteAddressBuffer g_renderObjectCount : register(u1);

bool is_sphere_inside_frustum(float4 boundsCenterRadius)
{
    const float radius = boundsCenterRadius.w;
    if (radius <= 0.0f)
    {
        return false;
    }

    const float4 viewCenter =
        mul(float4(boundsCenterRadius.xyz, 1.0f), g_viewMatrix);
    const float viewZ = viewCenter.z;

    const float projection00 = max(abs(g_projectionMatrix[0][0]), 0.000001f);
    const float projection11 = max(abs(g_projectionMatrix[1][1]), 0.000001f);
    const float tanHalfFovX = 1.0f / projection00;
    const float tanHalfFovY = 1.0f / projection11;

    const float projection22 = g_projectionMatrix[2][2];
    const float projection32 = g_projectionMatrix[3][2];
    const float nearClip = -projection32 / max(projection22 + 1.0f, 0.000001f);
    const float farClip = projection32 / min(1.0f - projection22, -0.000001f);

    if (viewZ + radius < nearClip || viewZ - radius > farClip)
    {
        return false;
    }

    const float horizontalLimit = abs(viewZ) * tanHalfFovX + radius;
    if (abs(viewCenter.x) > horizontalLimit)
    {
        return false;
    }

    const float verticalLimit = abs(viewZ) * tanHalfFovY + radius;
    if (abs(viewCenter.y) > verticalLimit)
    {
        return false;
    }

    return true;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint objectId = dispatchThreadId.x;

    RenderableInfo renderableInfo;
    bool visible = objectId < g_objectCount;
    if (visible)
    {
        renderableInfo = g_renderableInfos[objectId];
        visible =
            renderableInfo.visible != 0 &&
            is_sphere_inside_frustum(renderableInfo.boundsCenterRadius);
    }

    const uint localOffset = WavePrefixCountBits(visible);
    const uint waveVisibleCount = WaveActiveCountBits(visible);
    uint waveBaseIndex = 0;
    if (WaveIsFirstLane() && waveVisibleCount > 0)
    {
        g_renderObjectCount.InterlockedAdd(
            0,
            waveVisibleCount,
            waveBaseIndex);
    }
    waveBaseIndex = WaveReadLaneFirst(waveBaseIndex);

    if (!visible)
    {
        return;
    }

    RenderObject renderObject;
    renderObject.objectId = renderableInfo.objectId;
    renderObject.meshId = renderableInfo.meshId;
    renderObject.transformId = renderableInfo.transformId;
    renderObject.materialId = renderableInfo.materialId;
    renderObject.castsShadow = renderableInfo.castsShadow;
    renderObject.receivesShadow = renderableInfo.receivesShadow;
    renderObject.shadowCasterMode = renderableInfo.shadowCasterMode;
    renderObject.skinPaletteOffset = renderableInfo.skinPaletteOffset;
    renderObject.skinPaletteCount = renderableInfo.skinPaletteCount;
    g_renderObjects[waveBaseIndex + localOffset] = renderObject;
}
