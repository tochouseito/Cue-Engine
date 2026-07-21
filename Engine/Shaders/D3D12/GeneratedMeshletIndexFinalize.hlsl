#include "GeneratedMeshletIndexCommon.hlsli"

cbuffer PageIndexParam : register(b0)
{
    uint g_pageIndex;
};

cbuffer GeneratedIndexCapacityParam : register(b1)
{
    uint g_maxGeneratedIndexCount;
};

ByteAddressBuffer g_generatedCounters : register(t0);
RWByteAddressBuffer g_state : register(u0);
RWByteAddressBuffer g_indirectCommand : register(u1);
RWByteAddressBuffer g_indirectCommandCount : register(u2);

static const uint kGeneratedIndexCounterOffset = 0u;
static const uint kGeneratedOverflowOffset = 4u;
[numthreads(1, 1, 1)]
void CSMain()
{
    const uint generatedIndexCount =
        g_generatedCounters.Load(kGeneratedIndexCounterOffset);
    const uint generatedOverflow =
        g_generatedCounters.Load(kGeneratedOverflowOffset);
    const uint activePageCount =
        g_state.Load(kGeneratedStateActivePageCountOffset);
    const bool valid =
        g_state.Load(kGeneratedStateValidOffset) != 0u &&
        g_pageIndex < activePageCount &&
        generatedIndexCount != 0u &&
        generatedIndexCount <= g_maxGeneratedIndexCount &&
        (generatedIndexCount % 3u) == 0u &&
        generatedOverflow == 0u;

    if (valid)
    {
        g_indirectCommand.Store(0u, 0u);
        g_indirectCommand.Store(4u, 0u);
        g_indirectCommand.Store(8u, generatedIndexCount);
        g_indirectCommand.Store(12u, 1u);
        g_indirectCommand.Store(16u, 0u);
        g_indirectCommand.Store(20u, 0u);
        g_indirectCommand.Store(24u, 0u);
        g_indirectCommandCount.Store(0u, 1u);

        uint ignored = 0u;
        g_state.InterlockedAdd(kGeneratedStateIndexCountOffset,
                               generatedIndexCount, ignored);
        g_state.InterlockedAdd(kGeneratedStateDrawCountOffset, 1u, ignored);
    }
    else
    {
        g_indirectCommandCount.Store(0u, 0u);
        if (generatedOverflow != 0u ||
            generatedIndexCount > g_maxGeneratedIndexCount)
        {
            uint ignored = 0u;
            g_state.InterlockedOr(kGeneratedStateOverflowOffset, 1u, ignored);
        }
    }
}
