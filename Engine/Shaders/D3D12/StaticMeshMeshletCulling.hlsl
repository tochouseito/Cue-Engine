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

struct Transform
{
    row_major float4x4 worldMatrix;
    row_major float4x4 normalMatrix;
};

struct MeshRange
{
    uint indexCount;
    uint startIndex;
    int baseVertex;
    uint meshletOffset;
    uint meshletCount;
    uint padding0;
    uint padding1;
    uint padding2;
};

struct Meshlet
{
    uint startIndex;
    uint indexCount;
    int baseVertex;
    uint padding;
    float4 boundsCenterRadius;
    float4 coneApex;
    float4 coneAxisCutoff;
};

struct IndirectCommand
{
    uint drawObjectStartIndex;
    uint indexCountPerInstance;
    uint instanceCount;
    uint startIndexLocation;
    int baseVertexLocation;
    uint startInstanceLocation;
};

cbuffer MeshletCullLimit : register(b0)
{
    uint g_maxIndirectCommandCount;
};

cbuffer ViewProjection : register(b1)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
    float4 g_cameraPosition;
};

StructuredBuffer<RenderObject> g_renderObjects : register(t0);
StructuredBuffer<Transform> g_transforms : register(t1);
StructuredBuffer<MeshRange> g_meshRanges : register(t2);
ByteAddressBuffer g_renderObjectCount : register(t3);
StructuredBuffer<Meshlet> g_meshlets : register(t4);

RWStructuredBuffer<IndirectCommand> g_indirectCommands : register(u0);
RWByteAddressBuffer g_indirectCommandCount : register(u1);

float transform_radius(float4x4 worldMatrix, float localRadius)
{
    const float scaleX = length(float3(worldMatrix[0][0], worldMatrix[0][1], worldMatrix[0][2]));
    const float scaleY = length(float3(worldMatrix[1][0], worldMatrix[1][1], worldMatrix[1][2]));
    const float scaleZ = length(float3(worldMatrix[2][0], worldMatrix[2][1], worldMatrix[2][2]));
    return localRadius * max(scaleX, max(scaleY, scaleZ));
}

bool is_sphere_inside_frustum(float3 worldCenter, float radius)
{
    if (radius <= 0.0f)
    {
        return false;
    }

    const float4 viewCenter = mul(float4(worldCenter, 1.0f), g_viewMatrix);
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

bool is_meshlet_cone_visible(Meshlet meshlet, Transform transform)
{
    const float coneCutoff = meshlet.coneAxisCutoff.w;
    if (coneCutoff <= -1.0f)
    {
        return true;
    }

    const float3 worldApex =
        mul(float4(meshlet.coneApex.xyz, 1.0f), transform.worldMatrix).xyz;
    const float3 worldAxisVector =
        mul(float4(meshlet.coneAxisCutoff.xyz, 0.0f), transform.normalMatrix).xyz;
    const float axisLength = length(worldAxisVector);
    const float3 cameraToApexVector = worldApex - g_cameraPosition.xyz;
    const float cameraToApexLength = length(cameraToApexVector);
    if (axisLength <= 0.000001f || cameraToApexLength <= 0.000001f)
    {
        return true;
    }

    const float3 worldAxis = worldAxisVector / axisLength;
    const float3 cameraToApex = cameraToApexVector / cameraToApexLength;

    return dot(cameraToApex, worldAxis) < coneCutoff;
}

bool is_meshlet_visible(Meshlet meshlet, Transform transform)
{
    const float3 worldCenter =
        mul(float4(meshlet.boundsCenterRadius.xyz, 1.0f), transform.worldMatrix).xyz;
    const float worldRadius =
        transform_radius(transform.worldMatrix, meshlet.boundsCenterRadius.w);

    return is_sphere_inside_frustum(worldCenter, worldRadius) &&
        is_meshlet_cone_visible(meshlet, transform);
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint objectIndex = dispatchThreadId.x;
    const uint objectCount = g_renderObjectCount.Load(0);
    if (objectIndex >= objectCount)
    {
        return;
    }

    const RenderObject objectInfo = g_renderObjects[objectIndex];
    const Transform transform = g_transforms[objectInfo.transformId];
    const MeshRange meshRange = g_meshRanges[objectInfo.meshId];

    for (uint meshletIndex = 0; meshletIndex < meshRange.meshletCount; ++meshletIndex)
    {
        const Meshlet meshlet =
            g_meshlets[meshRange.meshletOffset + meshletIndex];
        if (!is_meshlet_visible(meshlet, transform))
        {
            continue;
        }

        uint dstIndex = 0;
        g_indirectCommandCount.InterlockedAdd(0, 1, dstIndex);
        if (dstIndex >= g_maxIndirectCommandCount)
        {
            continue;
        }

        IndirectCommand indirectCommand;
        indirectCommand.drawObjectStartIndex = objectIndex;
        indirectCommand.indexCountPerInstance = meshlet.indexCount;
        indirectCommand.instanceCount = 1;
        indirectCommand.startIndexLocation = meshlet.startIndex;
        indirectCommand.baseVertexLocation = meshlet.baseVertex;
        indirectCommand.startInstanceLocation = 0;
        g_indirectCommands[dstIndex] = indirectCommand;
    }
}
