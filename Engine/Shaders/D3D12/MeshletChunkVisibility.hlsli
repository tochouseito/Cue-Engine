#ifndef CUE_MESHLET_CHUNK_VISIBILITY_HLSLI
#define CUE_MESHLET_CHUNK_VISIBILITY_HLSLI

uint ChunkVisibilityWordIndex(uint globalChunkId)
{
    return globalChunkId >> 5u;
}

uint ChunkVisibilityBitMask(uint globalChunkId)
{
    return 1u << (globalChunkId & 31u);
}

bool WasChunkVisibleLastFrame(ByteAddressBuffer previousVisibilityBits,
                              uint globalChunkId)
{
    const uint wordIndex = ChunkVisibilityWordIndex(globalChunkId);
    const uint mask = ChunkVisibilityBitMask(globalChunkId);
    return (previousVisibilityBits.Load(wordIndex * 4u) & mask) != 0u;
}

bool WasChunkVisibleLastFrame(RWByteAddressBuffer previousVisibilityBits,
                              uint globalChunkId)
{
    const uint wordIndex = ChunkVisibilityWordIndex(globalChunkId);
    const uint mask = ChunkVisibilityBitMask(globalChunkId);
    return (previousVisibilityBits.Load(wordIndex * 4u) & mask) != 0u;
}

void MarkChunkVisibleThisFrame(RWByteAddressBuffer currentVisibilityBits,
                               uint globalChunkId)
{
    const uint wordIndex = ChunkVisibilityWordIndex(globalChunkId);
    const uint mask = ChunkVisibilityBitMask(globalChunkId);
    currentVisibilityBits.InterlockedOr(wordIndex * 4u, mask);
}

#endif
