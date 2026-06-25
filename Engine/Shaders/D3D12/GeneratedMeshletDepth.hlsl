// Depth-only draw from a generated non-indexed meshlet vertex stream.

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

struct Transform
{
    row_major float4x4 worldMatrix;
    row_major float4x4 normalMatrix;
};

struct GeneratedTriVertex
{
    uint objectIndex;
    uint sourceVertexIndex;
};

cbuffer ViewProjection : register(b0)
{
    row_major float4x4 g_viewMatrix;
    row_major float4x4 g_projectionMatrix;
};

StructuredBuffer<GeneratedTriVertex> g_generatedVertices : register(t0);
StructuredBuffer<float4> g_positions : register(t1);
StructuredBuffer<RenderObject> g_renderObjects : register(t2);
StructuredBuffer<Transform> g_transforms : register(t3);

float4 vs_main(uint vertexId : SV_VertexID) : SV_Position
{
    const GeneratedTriVertex generated = g_generatedVertices[vertexId];
    const RenderObject renderObject = g_renderObjects[generated.objectIndex];
    const Transform transform = g_transforms[renderObject.transformId];

    const float4 localPosition = g_positions[generated.sourceVertexIndex];
    const float4 worldPosition = mul(localPosition, transform.worldMatrix);
    const float4 viewPosition = mul(worldPosition, g_viewMatrix);
    return mul(viewPosition, g_projectionMatrix);
}

void ps_main()
{
}
