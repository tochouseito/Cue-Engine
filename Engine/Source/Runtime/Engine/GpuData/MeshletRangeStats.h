#pragma once

/// ****************************************************************************
/// Meshlet range rendering debug statistics
/// ****************************************************************************

// === C++ includes ===
#include <cstdint>

namespace Cue::GpuData {
struct MeshletRangeStatsGpu final {
  uint32_t candidateObjectCount = 0;
  uint32_t rangeDrawObjectCount = 0;
  uint32_t normalDrawObjectCount = 0;
  uint32_t culledObjectCount = 0;

  uint32_t fallbackObjectCount = 0;
  uint32_t rangeCommandCount = 0;
  uint32_t overflow = 0;
  uint32_t testedMeshletCount = 0;

  uint32_t visibleMeshletCount = 0;
  uint32_t culledMeshletCount = 0;
  uint32_t visibleIndexCount = 0;
  uint32_t rangeDrawnIndexCount = 0;

  uint32_t rangeCulledIndexCount = 0;
  uint32_t rangeExtraGapIndexCount = 0;
  uint32_t frustumCulledMeshletCount = 0;
  uint32_t hiZCulledMeshletCount = 0;
  uint32_t backfaceCulledMeshletCount = 0;
};
} // namespace Cue::GpuData
