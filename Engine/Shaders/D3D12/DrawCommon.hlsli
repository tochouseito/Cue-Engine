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
};

struct Transform
{
    row_major float4x4 worldMatrix;
};

struct MeshRange
{
    uint indexCount;
    uint startIndex;
    int baseVertex;
    uint padding;
};
