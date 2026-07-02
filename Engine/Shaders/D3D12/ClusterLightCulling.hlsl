// Optimized clustered point-light assignment。
// BuildClusterGrid の cluster AABB と PreparePointLights の view-space lights を使い、
// cluster ごとの light list を compact buffer に作る。

#define GROUP_SIZE 64
#define MAX_LIGHTS_PER_CLUSTER 64

// ViewPointLightBuffer に事前変換済みの point light。
// cluster assignment では view-space AABB と比較するため、ここでは
// world-space の light ではなく view-space の位置と半径だけを使う。
struct ViewPointLight
{
    float4 positionRadius;
    float4 colorIntensity;
    uint sourceIndex;
    uint padding0;
    uint padding1;
    uint padding2;
};

struct Cluster
{
    float4 minPoint;
    float4 maxPoint;
};

struct ClusterLightRange
{
    // ClusterLightIndexBuffer 内の light list 開始位置。
    // 同じ light list を持つ cluster は同じ offset/count を共有できる。
    uint offset;
    uint count;

    // list 共有判定用の fingerprint。実用上 hash だけでは危険なので、
    // count/first/last light id も併用して候補を絞る。
    uint hash;
    uint overflow;
};

// 1 thread が 1 cluster を担当し、workgroup 全体で light を shared memory に
// batch 読み込みする。culling で使う field だけを cache し、LDS 使用量を抑える。
groupshared float4 s_lightPositionRadii[GROUP_SIZE];
groupshared uint s_lightSourceIds[GROUP_SIZE];
groupshared uint s_hashes[GROUP_SIZE];
groupshared uint s_starts[GROUP_SIZE];
groupshared uint s_countOverflows[GROUP_SIZE];
groupshared uint s_firstLightIds[GROUP_SIZE];
groupshared uint s_lastLightIds[GROUP_SIZE];

StructuredBuffer<Cluster> g_clusters : register(t0);
StructuredBuffer<ViewPointLight> g_viewPointLights : register(t1);

RWStructuredBuffer<ClusterLightRange> g_clusterLightRanges : register(u0);
RWStructuredBuffer<uint> g_clusterLightIndices : register(u1);
RWByteAddressBuffer g_clusterLightItemCounter : register(u2);
RWByteAddressBuffer g_clusterLightStats : register(u3);

static const float kClusterAabbPaddingScale = 0.08f;
static const float kClusterLightRadiusCullScale = 1.15f;

// ClusterLightingStatsGpu の uint offset。ImGui で「cluster assignment が
// どれだけ compact できているか」を見るため、GPU 側で直接集計する。
static const uint k_statClusterCount = 0u;
static const uint k_statActiveClusterCount = 4u;
static const uint k_statPointLightCount = 8u;
static const uint k_statTotalClusterItems = 12u;
static const uint k_statMaxLightsInCluster = 16u;
static const uint k_statOverflowClusterCount = 20u;
static const uint k_statEmptyClusterCount = 24u;
static const uint k_statReusedListCount = 28u;

cbuffer ClusterCountParam : register(b0)
{
    uint g_clusterCount;
};

cbuffer PointLightCountParam : register(b1)
{
    uint g_pointLightCount;
};

cbuffer MaxClusterLightItemsParam : register(b2)
{
    uint g_maxClusterLightItems;
};

cbuffer MaxLightsPerClusterParam : register(b3)
{
    uint g_maxLightsPerCluster;
};

bool sphere_intersects_aabb(float3 center, float radius, float3 minPoint,
                            float3 maxPoint)
{
    const float3 padding =
        max((maxPoint - minPoint) * kClusterAabbPaddingScale,
            float3(0.001f, 0.001f, 0.001f));
    const float3 expandedMinPoint = minPoint - padding;
    const float3 expandedMaxPoint = maxPoint + padding;
    const float expandedRadius = radius * kClusterLightRadiusCullScale;
    const float3 closest = clamp(center, expandedMinPoint, expandedMaxPoint);
    const float3 delta = center - closest;
    return dot(delta, delta) <= expandedRadius * expandedRadius;
}

uint hash_init()
{
    return 2166136261u;
}

uint hash_next(uint hash, uint value)
{
    hash ^= value;
    hash *= 16777619u;
    return hash;
}

[numthreads(GROUP_SIZE, 1, 1)]
void CSMain(uint3 groupId : SV_GroupID,
            uint3 groupThreadId : SV_GroupThreadID)
{
    // この pass は「cluster ごとに、その cluster に影響する point light の
    // index list を作る」処理。forward PS はこの list だけを見て light 評価する。
    const uint localId = groupThreadId.x;
    const uint clusterIndex = groupId.x * GROUP_SIZE + localId;

    // private array にこの cluster の light list を一時構築する。
    // 後段で同じ list を持つ cluster を探し、代表 cluster だけが
    // ClusterLightIndexBuffer に書くことで buffer 使用量を抑える。
    uint localLightIds[MAX_LIGHTS_PER_CLUSTER];
    uint localLightCount = 0u;
    uint overflow = 0u;
    uint hash = hash_init();
    uint firstLightId = 0xffffffffu;
    uint lastLightId = 0xffffffffu;

    Cluster cluster;
    bool validCluster = clusterIndex < g_clusterCount;
    if (validCluster)
    {
        cluster = g_clusters[clusterIndex];
    }
    else
    {
        cluster.minPoint = float4(0.0f, 0.0f, 0.0f, 0.0f);
        cluster.maxPoint = float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    for (uint batchStart = 0u; batchStart < g_pointLightCount;
         batchStart += GROUP_SIZE)
    {
        // workgroup 内の 128 thread で 128 lights を shared memory へ読む。
        // 各 cluster thread は shared memory 上の light を使って交差判定する。
        const uint lightIndex = batchStart + localId;
        if (lightIndex < g_pointLightCount)
        {
            const ViewPointLight light = g_viewPointLights[lightIndex];
            s_lightPositionRadii[localId] = light.positionRadius;
            s_lightSourceIds[localId] = light.sourceIndex;
        }

        // 全 thread が s_lights を読み始める前に、batch load 完了を保証する。
        GroupMemoryBarrierWithGroupSync();

        const uint batchCount = min(GROUP_SIZE, g_pointLightCount - batchStart);
        if (validCluster)
        {
            for (uint i = 0u; i < batchCount; ++i)
            {
                const float4 lightPositionRadius = s_lightPositionRadii[i];
                const uint lightSourceIndex = s_lightSourceIds[i];

                // light sphere と cluster AABB の交差判定。
                // 交差した light だけを pixel shader の評価対象にする。
                if (sphere_intersects_aabb(lightPositionRadius.xyz,
                                           lightPositionRadius.w,
                                           cluster.minPoint.xyz,
                                           cluster.maxPoint.xyz))
                {
                    if (localLightCount < MAX_LIGHTS_PER_CLUSTER &&
                        localLightCount < g_maxLightsPerCluster)
                    {
                        localLightIds[localLightCount] = lightSourceIndex;
                        if (localLightCount == 0u)
                        {
                            firstLightId = lightSourceIndex;
                        }
                        lastLightId = lightSourceIndex;
                        ++localLightCount;
                        hash = hash_next(hash, lightSourceIndex);
                    }
                    else
                    {
                        // cluster に入る light が上限を超えた。
                        // 描画は切り詰めて継続し、stats で検出できるようにする。
                        overflow = 1u;
                    }
                }
            }
        }

        // 次 batch の s_lights 書き込みが、まだ読み終えていない thread と
        // 競合しないように同期する。
        GroupMemoryBarrierWithGroupSync();
    }

    // count も hash に混ぜる。空 list と特定の light 並びが偶然同じ hash
    // になるリスクを少し下げるため。
    hash = hash_next(hash, localLightCount);

    // 他 thread が同じ light list を持つか調べられるよう、fingerprint を
    // shared memory に公開する。private array 自体は他 thread から読めない。
    s_hashes[localId] = hash;
    s_starts[localId] = 0u;
    s_countOverflows[localId] = localLightCount | (overflow << 31u);
    s_firstLightIds[localId] = firstLightId;
    s_lastLightIds[localId] = lastLightId;

    GroupMemoryBarrierWithGroupSync();

    if (groupId.x == 0u && localId == 0u)
    {
        // point light 数は pass 全体で共通なので 1 thread だけが書く。
        g_clusterLightStats.Store(k_statPointLightCount, g_pointLightCount);
    }

    if (validCluster)
    {
        g_clusterLightStats.InterlockedAdd(k_statClusterCount, 1u);
        g_clusterLightStats.InterlockedAdd(k_statTotalClusterItems,
                                           localLightCount);
        if (localLightCount > 0u)
        {
            g_clusterLightStats.InterlockedAdd(k_statActiveClusterCount, 1u);
        }
        else
        {
            g_clusterLightStats.InterlockedAdd(k_statEmptyClusterCount, 1u);
        }
        uint previousMax = 0u;
        // 最も light が詰まった cluster を記録する。cluster grid の粗さや
        // maxLightsPerCluster の妥当性を判断するための値。
        g_clusterLightStats.InterlockedMax(k_statMaxLightsInCluster,
                                           localLightCount,
                                           previousMax);
    }

    uint firstLocalId = localId;
    if (validCluster)
    {
        [loop]
        for (uint i = 0u; i < GROUP_SIZE; ++i)
        {
            if (i > localId)
            {
                break;
            }

            if (s_hashes[i] == hash &&
                (s_countOverflows[i] & 0x7fffffffu) == localLightCount &&
                s_firstLightIds[i] == firstLightId &&
                s_lastLightIds[i] == lastLightId)
            {
                // 同じ fingerprint の最初の cluster を代表にする。
                // この thread は代表が書いた offset/count を再利用する。
                firstLocalId = i;
                break;
            }
            else if (s_hashes[i] == hash)
            {
                // Hash matched but count/first/last did not. Kept for GPU
                // debugging by folding it into overflowClusterCount.
                g_clusterLightStats.InterlockedAdd(
                    k_statOverflowClusterCount, 1u);
            }
        }
    }

    if (validCluster && firstLocalId == localId)
    {
        uint start = 0u;

        // 代表 cluster だけが compact index buffer に list を append する。
        // 非代表 cluster はこの書き込みを省略し、後で同じ start/count を参照する。
        g_clusterLightItemCounter.InterlockedAdd(0u, localLightCount, start);

        if (start + localLightCount <= g_maxClusterLightItems)
        {
            for (uint i = 0u; i < localLightCount; ++i)
            {
                g_clusterLightIndices[start + i] = localLightIds[i];
            }

            s_starts[localId] = start;
            s_countOverflows[localId] = localLightCount | (overflow << 31u);
        }
        else
        {
            // compact buffer 自体が満杯になった。範囲外参照を防ぐため
            // 空 list として保存し、stats で容量不足を見える化する。
            s_starts[localId] = 0u;
            s_countOverflows[localId] = 1u << 31u;
            g_clusterLightStats.InterlockedAdd(k_statOverflowClusterCount, 1u);
        }

        if (overflow != 0u)
        {
            g_clusterLightStats.InterlockedAdd(k_statOverflowClusterCount, 1u);
        }
    }
    else if (validCluster)
    {
        // 代表 cluster ではないので light list 書き込みを再利用できた。
        g_clusterLightStats.InterlockedAdd(k_statReusedListCount, 1u);
    }

    GroupMemoryBarrierWithGroupSync();

    if (validCluster)
    {
        // Forward pass が参照する cluster -> light list の対応表。
        // offset/count は compact buffer 内の範囲、hash/overflow は debug 用。
        ClusterLightRange range;
        range.offset = s_starts[firstLocalId];
        range.count = s_countOverflows[firstLocalId] & 0x7fffffffu;
        range.hash = hash;
        range.overflow = s_countOverflows[firstLocalId] >> 31u;

        g_clusterLightRanges[clusterIndex] = range;
    }
}
