// EffectPrimitiveResources の役割と公開要素を定義する

#pragma once

// === RHI includes ===
#include <RHI.h>

// === Engine includes ===
#include <GpuData/Effect.h>

// === C++ includes ===
#include <array>
#include <vector>

namespace Cue::EffectSystem {
enum class EffectPrimitiveResourceType : uint32_t {
  FrameBuffer = 0,
  SpriteBuffer,
  RibbonBuffer,
  Count
};

class EffectPrimitiveResources final {
public:
  EffectPrimitiveResources(RHI::IBufferManager *a_bufferManager,
                           uint32_t a_bufferCount)
      : m_bufferManager(a_bufferManager), m_bufferCount(a_bufferCount) {}

  Result create_frame_buffer();
  Result create_sprite_buffer(uint32_t a_maxSpriteCount);
  Result create_ribbon_buffer(uint32_t a_maxRibbonCount);

  std::vector<RHI::SlotUploader<GpuData::EffectFrameGpu>> &
  frame_uploaders() noexcept {
    return m_frameUploaders;
  }

  std::vector<RHI::SlotUploader<GpuData::EffectSpriteGpu>> &
  sprite_uploaders() noexcept {
    return m_spriteUploaders;
  }

  std::vector<RHI::SlotUploader<GpuData::EffectRibbonGpu>> &
  ribbon_uploaders() noexcept {
    return m_ribbonUploaders;
  }

  [[nodiscard]] RHI::BufferHandle frame_buffer_handle() const noexcept {
    return m_bufferHandles[static_cast<size_t>(
        EffectPrimitiveResourceType::FrameBuffer)];
  }

  [[nodiscard]] RHI::BufferHandle sprite_buffer_handle() const noexcept {
    return m_bufferHandles[static_cast<size_t>(
        EffectPrimitiveResourceType::SpriteBuffer)];
  }

  [[nodiscard]] RHI::BufferHandle ribbon_buffer_handle() const noexcept {
    return m_bufferHandles[static_cast<size_t>(
        EffectPrimitiveResourceType::RibbonBuffer)];
  }

private:
  RHI::IBufferManager *m_bufferManager = nullptr;
  uint32_t m_bufferCount = 1;

  std::array<RHI::BufferHandle,
             static_cast<size_t>(EffectPrimitiveResourceType::Count)>
      m_bufferHandles{};
  std::vector<RHI::SlotUploader<GpuData::EffectFrameGpu>> m_frameUploaders{};
  std::vector<RHI::SlotUploader<GpuData::EffectSpriteGpu>> m_spriteUploaders{};
  std::vector<RHI::SlotUploader<GpuData::EffectRibbonGpu>> m_ribbonUploaders{};
};
} // namespace Cue::EffectSystem
