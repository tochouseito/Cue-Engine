cbuffer DispatchParam : register(b0)
{
    uint g_maxMeshCount;
}

ByteAddressBuffer g_bucketCounts : register(t0);
RWByteAddressBuffer g_bucketOffsets : register(u0);
RWByteAddressBuffer g_bucketCursor : register(u1);

[numthreads(1, 1, 1)]
void CSMain()
{
    uint runningOffset = 0;
    for (uint meshId = 0; meshId < g_maxMeshCount; ++meshId)
    {
        const uint byteOffset = meshId * 4;
        g_bucketOffsets.Store(byteOffset, runningOffset);
        g_bucketCursor.Store(byteOffset, 0);
        runningOffset += g_bucketCounts.Load(byteOffset);
    }
}
