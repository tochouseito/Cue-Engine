// Standalone AS/MS/PS probe for D3D12 mesh PSO creation.

struct VisibleMeshlet
{
    uint objectIndex;
    uint meshId;
    uint meshletIndex;
    uint segmentStartIndex;
};

struct MeshShaderVisibilityPayload
{
    uint visibleMeshletIndex;
};

struct ProbeVertexOut
{
    float4 position : SV_POSITION;
};

struct ProbePrimitiveOut
{
    nointerpolation uint renderObjectIndex : TEXCOORD0;
    nointerpolation uint meshPrimitiveId : TEXCOORD1;
};

struct ProbePsInput
{
    float4 position : SV_POSITION;
    nointerpolation uint renderObjectIndex : TEXCOORD0;
    nointerpolation uint meshPrimitiveId : TEXCOORD1;
};

[numthreads(1, 1, 1)]
void as_main(uint3 groupId : SV_GroupID)
{
    MeshShaderVisibilityPayload payload;
    payload.visibleMeshletIndex = groupId.x;
    DispatchMesh(1, 1, 1, payload);
}

[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void ms_main(in payload MeshShaderVisibilityPayload payload,
             out vertices ProbeVertexOut vertices[3],
             out indices uint3 triangles[1],
             out primitives ProbePrimitiveOut primitives[1])
{
    SetMeshOutputCounts(3, 1);

    vertices[0].position = float4(-0.5f, -0.5f, 0.5f, 1.0f);
    vertices[1].position = float4(0.0f, 0.5f, 0.5f, 1.0f);
    vertices[2].position = float4(0.5f, -0.5f, 0.5f, 1.0f);
    triangles[0] = uint3(0u, 1u, 2u);
    primitives[0].renderObjectIndex = payload.visibleMeshletIndex;
    primitives[0].meshPrimitiveId = 0u;
}

uint ps_main(ProbePsInput input) : SV_Target0
{
    return input.renderObjectIndex + 1u;
}
