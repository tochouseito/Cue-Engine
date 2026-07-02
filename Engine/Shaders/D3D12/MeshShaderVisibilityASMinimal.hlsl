// Minimal AS/MS pipeline probe for D3D12 PSO creation.

struct MinimalPayload
{
    uint value;
};

struct MinimalVertexOut
{
    float4 position : SV_POSITION;
};

struct MinimalPrimitiveOut
{
    nointerpolation uint value : TEXCOORD0;
};

struct MinimalPsInput
{
    float4 position : SV_POSITION;
    nointerpolation uint value : TEXCOORD0;
};

[numthreads(1, 1, 1)]
void as_main()
{
    MinimalPayload payload;
    payload.value = 0u;
    DispatchMesh(1, 1, 1, payload);
}

[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void ms_main(in payload MinimalPayload payload,
             out vertices MinimalVertexOut vertices[3],
             out indices uint3 triangles[1],
             out primitives MinimalPrimitiveOut primitives[1])
{
    SetMeshOutputCounts(3, 1);

    vertices[0].position = float4(-0.5f, -0.5f, 0.5f, 1.0f);
    vertices[1].position = float4(0.0f, 0.5f, 0.5f, 1.0f);
    vertices[2].position = float4(0.5f, -0.5f, 0.5f, 1.0f);
    triangles[0] = uint3(0u, 1u, 2u);
    primitives[0].value = payload.value;
}

uint ps_main(MinimalPsInput input) : SV_Target0
{
    return input.value + 1u;
}
