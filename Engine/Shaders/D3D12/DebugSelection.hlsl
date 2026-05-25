cbuffer ViewProjection : register(b0)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
}

static const uint kMaxDebugSelectionItemCount = 64;
static const uint kShapeBox = 0;
static const uint kShapeCameraFrustum = 1;
static const uint kShapeLine = 2;
static const uint kShapeLightArrow = 3;
static const uint kBoxVertexCount = 24;
static const uint kCameraFrustumVertexCount = 30;
static const uint kLineVertexCount = 2;
static const uint kLightArrowVertexCount = 18;

struct DebugSelectionItem
{
    row_major float4x4 worldMatrix;
    float4 color;
    float4 camera;
    uint shape;
    uint isEnabled;
    uint padding0;
    uint padding1;
};

cbuffer DebugSelection : register(b1)
{
    uint g_itemCount;
    uint g_padding0;
    uint g_padding1;
    uint g_padding2;
    DebugSelectionItem g_items[kMaxDebugSelectionItemCount];
}

static const float3 kCorners[8] =
{
    { -0.5f, -0.5f, -0.5f },
    { 0.5f, -0.5f, -0.5f },
    { 0.5f, -0.5f, 0.5f },
    { -0.5f, -0.5f, 0.5f },
    { -0.5f, 0.5f, -0.5f },
    { 0.5f, 0.5f, -0.5f },
    { 0.5f, 0.5f, 0.5f },
    { -0.5f, 0.5f, 0.5f },
};

static const uint kLineVertexToCorner[24] =
{
    0, 1, 1, 2, 2, 3, 3, 0,
    4, 5, 5, 6, 6, 7, 7, 4,
    0, 4, 1, 5, 2, 6, 3, 7,
};

float3 make_camera_frustum_corner(uint cornerIndex, float4 camera)
{
    const bool isFar = cornerIndex >= 4;
    const uint planeCornerIndex = isFar ? cornerIndex - 4 : cornerIndex;
    const float distance = isFar ? camera.w : camera.z;
    const float halfHeight = distance * tan(radians(camera.x) * 0.5f);
    const float halfWidth = halfHeight * camera.y;
    const float x = (planeCornerIndex == 1 || planeCornerIndex == 2)
        ? halfWidth
        : -halfWidth;
    const float y = planeCornerIndex >= 2 ? halfHeight : -halfHeight;
    return float3(x, y, distance);
}

float3 make_camera_up_marker_vertex(uint markerIndex, float4 camera)
{
    const float distance = camera.w;
    const float halfHeight = distance * tan(radians(camera.x) * 0.5f);
    const float halfWidth = halfHeight * camera.y;
    const float markerHalfWidth = min(halfWidth, halfHeight) * 0.38f;
    const float markerHeight = halfHeight * 0.52f;
    const float3 baseLeft = float3(-markerHalfWidth, halfHeight, distance);
    const float3 baseRight = float3(markerHalfWidth, halfHeight, distance);
    const float3 apex = float3(0.0f, halfHeight + markerHeight, distance);

    if (markerIndex == 0 || markerIndex == 5)
    {
        return baseLeft;
    }
    if (markerIndex == 1 || markerIndex == 2)
    {
        return apex;
    }
    return baseRight;
}

float3 make_light_arrow_vertex(uint vertexIndex, float4 camera)
{
    const float length = camera.x;
    const float headLength = camera.y;
    const float headWidth = camera.z;
    const float tailLength = length - headLength;
    const float3 origin = float3(0.0f, 0.0f, 0.0f);
    const float3 tip = float3(0.0f, 0.0f, -length);
    const float3 neck = float3(0.0f, 0.0f, -tailLength);
    const float3 right = float3(headWidth, 0.0f, -tailLength);
    const float3 left = float3(-headWidth, 0.0f, -tailLength);
    const float3 up = float3(0.0f, headWidth, -tailLength);
    const float3 down = float3(0.0f, -headWidth, -tailLength);

    switch (vertexIndex)
    {
    case 0:
        return origin;
    case 1:
        return tip;
    case 2:
        return tip;
    case 3:
        return right;
    case 4:
        return tip;
    case 5:
        return left;
    case 6:
        return tip;
    case 7:
        return up;
    case 8:
        return tip;
    case 9:
        return down;
    case 10:
        return right;
    case 11:
        return up;
    case 12:
        return up;
    case 13:
        return left;
    case 14:
        return left;
    case 15:
        return down;
    case 16:
        return down;
    case 17:
        return right;
    default:
        return neck;
    }
}

struct VsOut
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
};

VsOut vs_main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const uint itemIndex = min(instanceId, kMaxDebugSelectionItemCount - 1);
    const DebugSelectionItem item = g_items[itemIndex];
    const bool isEnabled = instanceId < g_itemCount && item.isEnabled != 0;
    const bool isLine = item.shape == kShapeLine;
    const bool isCameraFrustum = item.shape == kShapeCameraFrustum;
    const bool isLightArrow = item.shape == kShapeLightArrow;
    const bool isVisibleVertex = isLine
        ? vertexId < kLineVertexCount
        : (isLightArrow
            ? vertexId < kLightArrowVertexCount
            : (isCameraFrustum
                ? vertexId < kCameraFrustumVertexCount
                : vertexId < kBoxVertexCount));
    const uint cornerIndex =
        kLineVertexToCorner[min(vertexId, kBoxVertexCount - 1)];
    const uint markerIndex =
        vertexId < kBoxVertexCount ? 0 : vertexId - kBoxVertexCount;
    const float3 lineCorner =
        vertexId == 0 ? float3(0.0f, 0.0f, 0.0f) : item.camera.xyz;
    const float3 frustumCorner = vertexId < kBoxVertexCount
        ? make_camera_frustum_corner(cornerIndex, item.camera)
        : make_camera_up_marker_vertex(markerIndex, item.camera);
    const float3 arrowCorner = make_light_arrow_vertex(vertexId, item.camera);
    const float3 localCorner = isLine
        ? lineCorner
        : (isLightArrow
            ? arrowCorner
            : (isCameraFrustum ? frustumCorner : kCorners[cornerIndex]));
    float4 localPosition = float4(localCorner, 1.0f);
    if (!isEnabled || !isVisibleVertex)
    {
        localPosition = float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    const float4 worldPosition = mul(localPosition, item.worldMatrix);
    const float4 viewPosition = mul(worldPosition, g_viewMatrix);

    VsOut output;
    output.position = mul(viewPosition, g_projectionMatrix);
    output.color = (!isEnabled || !isVisibleVertex)
        ? float4(0.0f, 0.0f, 0.0f, 0.0f)
        : item.color;
    return output;
}

float4 ps_main(VsOut input) : SV_Target0
{
    return input.color;
}
