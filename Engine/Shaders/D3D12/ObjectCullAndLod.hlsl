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
    uint lodMeshId0;
    uint lodMeshId1;
    uint lodMeshId2;
    uint lodMeshId3;
    uint lodMeshId4;
    uint lodCount;
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
    uint drawFlags;
    uint depthBin;
    uint padding;
    float4 boundsCenterRadius;
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

uint get_lod_mesh_id(RenderableInfo renderableInfo, uint lodIndex)
{
    if (lodIndex == 1)
    {
        return renderableInfo.lodMeshId1;
    }
    if (lodIndex == 2)
    {
        return renderableInfo.lodMeshId2;
    }
    if (lodIndex == 3)
    {
        return renderableInfo.lodMeshId3;
    }
    if (lodIndex == 4)
    {
        return renderableInfo.lodMeshId4;
    }
    return renderableInfo.lodMeshId0;
}

float projected_radius(RenderableInfo renderableInfo, float viewZ)
{
    return renderableInfo.boundsCenterRadius.w *
        abs(g_projectionMatrix[1][1]) /
        max(viewZ, 0.001f);
}

bool is_valuable_occluder(RenderableInfo renderableInfo)
{
    const float4 viewCenter =
        mul(float4(renderableInfo.boundsCenterRadius.xyz, 1.0f), g_viewMatrix);
    const float viewZ = max(viewCenter.z, 0.001f);
    const float screenRadius = projected_radius(renderableInfo, viewZ);

    const bool largeOnScreen = screenRadius >= 0.08f;
    const bool nearEnough = viewZ <= 24.0f && screenRadius >= 0.025f;
    return largeOnScreen || nearEnough;
}

uint select_occluder_lod(RenderableInfo renderableInfo)
{
    const uint lodCount = max(renderableInfo.lodCount, 1u);
    return min(2u, lodCount - 1u);
}

float project_device_depth(float viewZ)
{
    const float4 clipPosition =
        mul(float4(0.0f, 0.0f, viewZ, 1.0f), g_projectionMatrix);
    if (abs(clipPosition.w) <= 0.000001f)
    {
        return 1.0f;
    }

    return saturate(clipPosition.z / clipPosition.w);
}

uint select_depth_bin(float4 boundsCenterRadius)
{
    const float4 viewCenter =
        mul(float4(boundsCenterRadius.xyz, 1.0f), g_viewMatrix);
    const float deviceDepth = project_device_depth(max(viewCenter.z, 0.001f));
    return min((uint)floor(saturate(deviceDepth) * 8.0f), 7u);
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint objectId = dispatchThreadId.x;
    if (objectId >= g_objectCount)
    {
        return;
    }

    const RenderableInfo renderableInfo = g_renderableInfos[objectId];
    if (renderableInfo.visible == 0u ||
        !is_sphere_inside_frustum(renderableInfo.boundsCenterRadius) ||
        !is_valuable_occluder(renderableInfo))
    {
        return;
    }

    uint objectOffset = 0u;
    g_renderObjectCount.InterlockedAdd(0, 1u, objectOffset);
    if (objectOffset >= g_objectCount)
    {
        return;
    }

    const uint lodIndex = select_occluder_lod(renderableInfo);
    RenderObject renderObject;
    renderObject.objectId = renderableInfo.objectId;
    renderObject.meshId = get_lod_mesh_id(renderableInfo, lodIndex);
    renderObject.transformId = renderableInfo.transformId;
    renderObject.materialId = renderableInfo.materialId;
    renderObject.castsShadow = renderableInfo.castsShadow;
    renderObject.receivesShadow = renderableInfo.receivesShadow;
    renderObject.shadowCasterMode = renderableInfo.shadowCasterMode;
    renderObject.skinPaletteOffset = renderableInfo.skinPaletteOffset;
    renderObject.skinPaletteCount = renderableInfo.skinPaletteCount;
    renderObject.drawFlags = 0u;
    renderObject.depthBin =
        select_depth_bin(renderableInfo.boundsCenterRadius);
    renderObject.padding = 0u;
    renderObject.boundsCenterRadius = renderableInfo.boundsCenterRadius;
    g_renderObjects[objectOffset] = renderObject;
}
