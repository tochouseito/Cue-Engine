cbuffer StatsParams : register(b0)
{
    uint g_visibilityWordCount;
};

RWByteAddressBuffer g_counters : register(u0);
RWByteAddressBuffer g_currentVisibilityBits : register(u1);
RWByteAddressBuffer g_previousVisibilityBits : register(u2);
RWByteAddressBuffer g_stats : register(u3);

static const uint kCounterCommandCount = 0u;
static const uint kCounterInstanceCount = 4u;

static const uint kStatsCommandCount = 0u;
static const uint kStatsInstanceCount = 4u;
static const uint kStatsCurrentVisibleChunkCount = 8u;
static const uint kStatsPreviousVisibleChunkCount = 12u;

uint count_bits(uint value)
{
    return countbits(value);
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint wordIndex = dispatchThreadId.x;
    if (wordIndex == 0u)
    {
        g_stats.Store(kStatsCommandCount,
                      g_counters.Load(kCounterCommandCount));
        g_stats.Store(kStatsInstanceCount,
                      g_counters.Load(kCounterInstanceCount));
    }

    if (wordIndex >= g_visibilityWordCount)
    {
        return;
    }

    const uint byteOffset = wordIndex * 4u;
    const uint currentBits = g_currentVisibilityBits.Load(byteOffset);
    const uint previousBits = g_previousVisibilityBits.Load(byteOffset);

    g_stats.InterlockedAdd(kStatsCurrentVisibleChunkCount,
                           count_bits(currentBits));
    g_stats.InterlockedAdd(kStatsPreviousVisibleChunkCount,
                           count_bits(previousBits));
}
