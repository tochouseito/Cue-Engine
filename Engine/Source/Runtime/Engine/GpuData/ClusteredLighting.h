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
        // ClusterLightIndexBuffer 内の固定 light list 参照。
        // hash/overflow は clustered-lighting debug view が使用する。
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

} // namespace Cue::GpuData
