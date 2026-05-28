// Share draw, material, light, and transform payloads across passes to keep register contracts consistent.

struct RenderableInfo
{
    uint id;
    uint visible;
    uint meshId;
    uint transformId;
    uint materialId;
    uint castsShadow;
    uint receivesShadow;
    uint shadowCasterMode;
    uint skinPaletteOffset;
    uint skinPaletteCount;
};

struct RenderObject
{
    uint id;
    uint meshId;
    uint transformId;
    uint materialId;
    uint castsShadow;
    uint receivesShadow;
    uint shadowCasterMode;
    uint skinPaletteOffset;
    uint skinPaletteCount;
};

static const uint k_shadowCasterModeSolid = 0u;
static const uint k_shadowCasterModeTwoSided = 1u;

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

struct SkinInfluence
{
    uint4 jointIndices;
    float4 weights;
};

struct SkinPalette
{
    row_major float4x4 matrix;
};
