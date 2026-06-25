// EffectPrimitiveFrameState の役割と公開要素を定義する

#pragma once

// === Engine includes ===
#include <GpuData/Effect.h>

// === C++ includes ===
#include <vector>

namespace Cue::EffectSystem {
struct EffectPrimitiveFrameData final {
  GpuData::EffectFrameGpu frame{};
};

struct EffectPrimitiveFrameState final {
  void resize(const uint32_t a_bufferCount) {
    frameStates.resize(a_bufferCount);
  }

  EffectPrimitiveFrameData &frame_state(const uint32_t a_bufferIndex) noexcept {
    return frameStates[a_bufferIndex];
  }

  const EffectPrimitiveFrameData &
  frame_state(const uint32_t a_bufferIndex) const noexcept {
    return frameStates[a_bufferIndex];
  }

  std::vector<EffectPrimitiveFrameData> frameStates{};
};
} // namespace Cue::EffectSystem
