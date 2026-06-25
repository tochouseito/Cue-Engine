// EffectPrimitiveScene の役割と公開要素を定義する

#pragma once

// === Engine includes ===
#include <GpuData/Effect.h>

// === C++ includes ===
#include <vector>

namespace Cue::EffectSystem {
struct EffectSpriteItem final {
  GpuData::EffectSpriteGpu sprite{};
};

struct EffectRibbonItem final {
  GpuData::EffectRibbonGpu ribbon{};
};

struct EffectPrimitiveSceneFrame final {
  std::vector<EffectSpriteItem> sprites{};
  std::vector<EffectRibbonItem> ribbons{};
};

class EffectPrimitiveScene final {
public:
  void resize(uint32_t a_bufferCount) { m_frames.resize(a_bufferCount); }

  void begin_frame(uint32_t a_bufferIndex) {
    if (a_bufferIndex >= m_frames.size()) {
      return;
    }

    m_frames[a_bufferIndex].sprites.clear();
    m_frames[a_bufferIndex].ribbons.clear();
  }

  [[nodiscard]] bool submit_sprite(uint32_t a_bufferIndex,
                                   const EffectSpriteItem &a_item) {
    if (a_bufferIndex >= m_frames.size()) {
      return false;
    }

    m_frames[a_bufferIndex].sprites.push_back(a_item);
    return true;
  }

  [[nodiscard]] bool submit_ribbon(uint32_t a_bufferIndex,
                                   const EffectRibbonItem &a_item) {
    if (a_bufferIndex >= m_frames.size()) {
      return false;
    }

    m_frames[a_bufferIndex].ribbons.push_back(a_item);
    return true;
  }

  EffectPrimitiveSceneFrame &frame(uint32_t a_bufferIndex) {
    return m_frames[a_bufferIndex];
  }

  const EffectPrimitiveSceneFrame &frame(uint32_t a_bufferIndex) const {
    return m_frames[a_bufferIndex];
  }

private:
  std::vector<EffectPrimitiveSceneFrame> m_frames{};
};
} // namespace Cue::EffectSystem
