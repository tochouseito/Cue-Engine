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
    float diffuseStrength;
    float specularStrength;
    float shininess;
    float padding1;
    float padding2;
    float padding3;
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
    uint padding;
};
