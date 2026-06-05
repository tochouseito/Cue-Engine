#pragma once

/// ****************************************************************************
/// Clustered forward GPU data
/// ****************************************************************************

// === Math includes ===
#include <CueMath.h>

// === C++ includes ===
#include <cstdint>

namespace Cue::GpuData
{
    struct ClusterGpu final
    {
        // BuildClusterGridPass が作る view-space AABB。
        // ClusterLightCulling は point light sphere とこの AABB を交差判定する。
        Math::float4 minPoint{};
        Math::float4 maxPoint{};
    };

    struct ClusterLightRangeGpu final
    {
        // ClusterLightIndexBuffer 内の compact light list 参照。
        // hash/overflow は list sharing と debug 表示のために保持する。
        uint32_t offset = 0;
        uint32_t count = 0;
        uint32_t hash = 0;
        uint32_t overflow = 0;
    };

    struct ViewPointLightGpu final
    {
        // PreparePointLightsPass が world-space light を view-space に変換した結果。
        // sourceIndex は forward shader が元の PointLightBuffer を読むために使う。
        Math::float4 positionRadius{};
        Math::float4 colorIntensity{};
        uint32_t sourceIndex = 0;
        uint32_t padding0 = 0;
        uint32_t padding1 = 0;
        uint32_t padding2 = 0;
    };

    struct ClusterLightingStatsGpu final
    {
        // ClusterLightCulling が GPU 上で集計し、readback して ImGui に表示する値。
        // clustered lighting の grid 粒度、list compaction、overflow を評価する。
        uint32_t clusterCount = 0;
        uint32_t activeClusterCount = 0;
        uint32_t pointLightCount = 0;
        uint32_t totalClusterItems = 0;

        uint32_t maxLightsInCluster = 0;
        uint32_t overflowClusterCount = 0;
        uint32_t emptyClusterCount = 0;
        uint32_t reusedListCount = 0;
    };
} // namespace Cue::GpuData
