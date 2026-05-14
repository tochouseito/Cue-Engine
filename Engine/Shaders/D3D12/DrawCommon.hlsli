struct RenderableInfo
{
    uint id;
    uint visible;
    uint meshId;
    uint transformId;
    uint materialId;
};

struct RenderObject
{
    uint id;
    uint meshId;
    uint transformId;
    uint materialId;
};

struct Material
{
    float4 color;
    uint textureId;
    uint useTexture;
    uint useReflectionSkybox;
    float shininess;
};

struct Transform
{
    row_major float4x4 worldMatrix;
    row_major float4x4 normalMatrix;
};

struct LightFrame
{
    float4 ambientColorIntensity;
    uint directionalLightCount;
    uint pointLightCount;
    uint spotLightCount;
    uint padding;
};

struct DirectionalLight
{
    float4 directionIntensity;
    float4 color;
};

struct PointLight
{
    float4 positionRange;
    float4 colorIntensity;
};

struct SpotLight
{
    float4 positionRange;
    float4 directionOuterCos;
    float4 colorIntensity;
};

struct SpotShadowFrame
{
    row_major float4x4 view;
    row_major float4x4 projection;
    float4 atlas;
    float4 params;
    float4 tuning;
};

struct DirectionalShadowFrame
{
    row_major float4x4 view;
    row_major float4x4 projection;
    float4 params;
    float4 tuning;
};

struct PointShadowFace
{
    row_major float4x4 view;
    row_major float4x4 projection;
    float4 atlas;
    float4 params;
    float4 tuning;
    float4 lightPositionRange;
};

struct MeshRange
{
    uint indexCount;
    uint startIndex;
    int baseVertex;
    uint padding;
};
