struct ObjectInfo
{
    uint id;
    uint visible;
    uint meshId;
    uint transformId;
};

struct RenderObject
{
    uint id;
    uint meshId;
    uint transformId;
};

struct Transform
{
    float4x4 worldMatrix;
};

struct MeshRange
{
    uint indexCount;
    uint startIndex;
    int baseVertex;
    uint padding;
};
