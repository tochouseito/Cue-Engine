// AS payload/SRV PSO creation probe for D3D12.

struct VisibleMeshlet
{
    uint objectIndex;
    uint meshId;
    uint meshletIndex;
    uint segmentStartIndex;
};

struct ProbePayload
{
    VisibleMeshlet visibleMeshlet;
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

ByteAddressBuffer g_visibleMeshlets : register(t0);

[numthreads(1, 1, 1)]
void as_main_payload_probe(uint3 groupId : SV_GroupID)
{
    ProbePayload payload;
    const uint4 packed = g_visibleMeshlets.Load4(groupId.x * 16u);
    payload.visibleMeshlet.objectIndex = packed.x;
    payload.visibleMeshlet.meshId = packed.y;
    payload.visibleMeshlet.meshletIndex = packed.z;
    payload.visibleMeshlet.segmentStartIndex = packed.w;
    DispatchMesh(1, 1, 1, payload);
}

[numthreads(1, 1, 1)]
void as_main_literal_payload_probe()
{
    ProbePayload payload;
    payload.visibleMeshlet = (VisibleMeshlet)0;
    DispatchMesh(1, 1, 1, payload);
}

[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void ms_main_payload_probe(in payload ProbePayload payload,
                           out vertices ProbeVertexOut vertices[3],
                           out indices uint3 triangles[1],
                           out primitives ProbePrimitiveOut primitives[1])
{
    SetMeshOutputCounts(3, 1);

    vertices[0].position = float4(-0.5f, -0.5f, 0.5f, 1.0f);
    vertices[1].position = float4(0.0f, 0.5f, 0.5f, 1.0f);
    vertices[2].position = float4(0.5f, -0.5f, 0.5f, 1.0f);
    triangles[0] = uint3(0u, 1u, 2u);
    primitives[0].renderObjectIndex = payload.visibleMeshlet.objectIndex;
    primitives[0].meshPrimitiveId = 0u;
}

uint ps_main_payload_probe(ProbePsInput input) : SV_Target0
{
    return input.renderObjectIndex + 1u;
}
