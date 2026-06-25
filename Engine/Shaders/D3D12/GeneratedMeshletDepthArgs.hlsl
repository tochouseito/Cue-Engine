// Build DrawInstancedIndirect arguments for the generated depth stream.

struct DrawInstancedArgs
{
    uint vertexCountPerInstance;
    uint instanceCount;
    uint startVertexLocation;
    uint startInstanceLocation;
};

ByteAddressBuffer g_generatedVertexCount : register(t0);
RWStructuredBuffer<DrawInstancedArgs> g_drawArgs : register(u0);

static const uint kGeneratedVertexCounterOffset = 4u;

[numthreads(1, 1, 1)]
void CSMain()
{
    const uint vertexCount =
        g_generatedVertexCount.Load(kGeneratedVertexCounterOffset);

    DrawInstancedArgs args;
    args.vertexCountPerInstance = vertexCount;
    args.instanceCount = vertexCount > 0u ? 1u : 0u;
    args.startVertexLocation = 0u;
    args.startInstanceLocation = 0u;
    g_drawArgs[0] = args;
}
