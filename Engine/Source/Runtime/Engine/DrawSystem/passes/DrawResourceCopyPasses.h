#pragma once

/// ****************************************************************************
/// Draw resource upload copy passes
/// ****************************************************************************

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include "DrawSystem/DrawFrameState.h"
#include "GpuData/Batching.h"
#include "GpuData/Transform.h"
#include "GpuData/ViewProjection.h"

// === C++ includes ===
#include <cstdint>
#include <limits>

namespace Cue::DrawSystem {
class UploadCopyVersion final {
public:
  explicit UploadCopyVersion(const uint64_t &a_version) noexcept
      : m_version(a_version) {}

  [[nodiscard]] bool should_copy() const noexcept {
    return m_copiedVersion != m_version;
  }

  void mark_copied() noexcept { m_copiedVersion = m_version; }

private:
  const uint64_t &m_version;
  uint64_t m_copiedVersion = (std::numeric_limits<uint64_t>::max)();
};

class RenderableInfoCopyPass final : public RHI::FrameGraphPass {
public:
  RenderableInfoCopyPass(const DrawFrameState &drawFrameState,
                         RHI::BufferHandle renderableInfoBuffer,
                         const uint64_t &a_uploadVersion)
      : m_drawFrameState(drawFrameState),
        m_renderableInfoBuffer(renderableInfoBuffer),
        m_copyVersion(a_uploadVersion) {}

  const char *name() const noexcept override { return "RenderableInfoCopy"; }
  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Copy;
  }
  bool is_enabled(uint32_t a_frameIndex) const noexcept override {
    return m_renderableInfoBuffer.valid() && m_copyVersion.should_copy() &&
           m_drawFrameState.frame_state(a_frameIndex).objectCount > 0u;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    return builder.read_buffer(m_renderableInfoBuffer);
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    return builder.use_buffer(
        m_renderableInfoBuffer, RHI::ResourceAccessType::Write,
        RHI::ResourceState::CopyDest, RHI::ResourceState::Common);
  }

  void execute(RHI::FrameGraphContext &context) override {
    const DrawFrameData &frameState =
        m_drawFrameState.frame_state(context.frame_index());
    if (!m_renderableInfoBuffer.valid() || frameState.objectCount == 0) {
      return;
    }

    RHI::BufferCopyRegion region{};
    region.srcBufferHandle = m_renderableInfoBuffer;
    region.srcUploadResourceIndex = context.frame_index();
    region.dstBufferHandle = m_renderableInfoBuffer;
    region.byteSize = static_cast<uint64_t>(frameState.objectCount) *
                      sizeof(GpuData::RenderableInfo);
    Result result = context.commandContext()->copy_buffer_region(region);
    if (result) {
      m_copyVersion.mark_copied();
    }
  }

private:
  const DrawFrameState &m_drawFrameState;
  RHI::BufferHandle m_renderableInfoBuffer{};
  mutable UploadCopyVersion m_copyVersion;
};

class TransformBufferCopyPass final : public RHI::FrameGraphPass {
public:
  TransformBufferCopyPass(const DrawFrameState &drawFrameState,
                          RHI::BufferHandle transformBuffer,
                          const uint64_t &a_uploadVersion)
      : m_drawFrameState(drawFrameState), m_transformBuffer(transformBuffer),
        m_copyVersion(a_uploadVersion) {}

  const char *name() const noexcept override { return "TransformBufferCopy"; }
  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Copy;
  }
  bool is_enabled(uint32_t a_frameIndex) const noexcept override {
    return m_transformBuffer.valid() && m_copyVersion.should_copy() &&
           m_drawFrameState.frame_state(a_frameIndex).objectCount > 0u;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    return builder.read_buffer(m_transformBuffer);
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    return builder.use_buffer(m_transformBuffer, RHI::ResourceAccessType::Write,
                              RHI::ResourceState::CopyDest,
                              RHI::ResourceState::Common);
  }

  void execute(RHI::FrameGraphContext &context) override {
    const DrawFrameData &frameState =
        m_drawFrameState.frame_state(context.frame_index());
    if (!m_transformBuffer.valid() || frameState.objectCount == 0) {
      return;
    }

    RHI::BufferCopyRegion region{};
    region.srcBufferHandle = m_transformBuffer;
    region.srcUploadResourceIndex = context.frame_index();
    region.dstBufferHandle = m_transformBuffer;
    region.byteSize = static_cast<uint64_t>(frameState.objectCount) *
                      sizeof(GpuData::ObjectTransformGpu);
    Result result = context.commandContext()->copy_buffer_region(region);
    if (result) {
      m_copyVersion.mark_copied();
    }
  }

private:
  const DrawFrameState &m_drawFrameState;
  RHI::BufferHandle m_transformBuffer{};
  mutable UploadCopyVersion m_copyVersion;
};

class ViewProjectionCopyPass final : public RHI::FrameGraphPass {
public:
  ViewProjectionCopyPass(const char* passName,
                         RHI::BufferHandle viewProjectionBuffer,
                         const uint64_t &a_uploadVersion,
                         uint32_t queueLane = 0)
      : m_passName(passName), m_viewProjectionBuffer(viewProjectionBuffer),
        m_copyVersion(a_uploadVersion), m_queueLane(queueLane) {}

  const char *name() const noexcept override { return m_passName; }
  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Copy;
  }
  uint32_t queue_lane() const noexcept override { return m_queueLane; }
  bool is_enabled(uint32_t) const noexcept override {
    return m_viewProjectionBuffer.valid() && m_copyVersion.should_copy();
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    return builder.read_buffer(m_viewProjectionBuffer);
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    return builder.use_buffer(
        m_viewProjectionBuffer, RHI::ResourceAccessType::Write,
        RHI::ResourceState::CopyDest, RHI::ResourceState::Common);
  }

  void execute(RHI::FrameGraphContext &context) override {
    if (!m_viewProjectionBuffer.valid()) {
      return;
    }

    RHI::BufferCopyRegion region{};
    region.srcBufferHandle = m_viewProjectionBuffer;
    region.srcUploadResourceIndex = context.frame_index();
    region.dstBufferHandle = m_viewProjectionBuffer;
    region.byteSize = sizeof(GpuData::ViewProjectionGpu);
    Result result = context.commandContext()->copy_buffer_region(region);
    if (result) {
      m_copyVersion.mark_copied();
    }
  }

private:
  const char* m_passName = "ViewProjectionCopy";
  RHI::BufferHandle m_viewProjectionBuffer{};
  mutable UploadCopyVersion m_copyVersion;
  uint32_t m_queueLane = 0;
};

class MaterialBufferCopyPass final : public RHI::FrameGraphPass {
public:
  explicit MaterialBufferCopyPass(RHI::BufferHandle materialBuffer,
                                  const uint64_t &a_uploadVersion,
                                  uint32_t queueLane = 0)
      : m_materialBuffer(materialBuffer), m_copyVersion(a_uploadVersion),
        m_queueLane(queueLane) {}

  const char *name() const noexcept override { return "MaterialBufferCopy"; }
  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Copy;
  }
  uint32_t queue_lane() const noexcept override { return m_queueLane; }
  bool is_enabled(uint32_t) const noexcept override {
    return m_materialBuffer.valid() && m_copyVersion.should_copy();
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    return builder.read_buffer(m_materialBuffer);
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    return builder.use_buffer(m_materialBuffer, RHI::ResourceAccessType::Write,
                              RHI::ResourceState::CopyDest,
                              RHI::ResourceState::Common);
  }

  void execute(RHI::FrameGraphContext &context) override {
    if (!m_materialBuffer.valid()) {
      return;
    }

    RHI::BufferCopyRegion region{};
    region.srcBufferHandle = m_materialBuffer;
    region.srcUploadResourceIndex = context.frame_index();
    region.dstBufferHandle = m_materialBuffer;
    region.byteSize = sizeof(GpuData::MaterialGpu);
    Result result = context.commandContext()->copy_buffer_region(region);
    if (result) {
      m_copyVersion.mark_copied();
    }
  }

private:
  RHI::BufferHandle m_materialBuffer{};
  mutable UploadCopyVersion m_copyVersion;
  uint32_t m_queueLane = 0;
};

class RenderCellCopyPass final : public RHI::FrameGraphPass {
public:
  RenderCellCopyPass(const DrawFrameState &drawFrameState,
                     RHI::BufferHandle renderCellBuffer,
                     const uint64_t &a_uploadVersion)
      : m_drawFrameState(drawFrameState), m_renderCellBuffer(renderCellBuffer),
        m_copyVersion(a_uploadVersion) {}

  const char *name() const noexcept override { return "RenderCellCopy"; }
  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Copy;
  }
  bool is_enabled(uint32_t a_frameIndex) const noexcept override {
    return m_renderCellBuffer.valid() && m_copyVersion.should_copy() &&
           m_drawFrameState.frame_state(a_frameIndex).cellCount > 0u;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    return builder.read_buffer(m_renderCellBuffer);
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    return builder.use_buffer(
        m_renderCellBuffer, RHI::ResourceAccessType::Write,
        RHI::ResourceState::CopyDest, RHI::ResourceState::Common);
  }

  void execute(RHI::FrameGraphContext &context) override {
    const DrawFrameData &frameState =
        m_drawFrameState.frame_state(context.frame_index());
    if (!m_renderCellBuffer.valid() || frameState.cellCount == 0) {
      return;
    }

    RHI::BufferCopyRegion region{};
    region.srcBufferHandle = m_renderCellBuffer;
    region.srcUploadResourceIndex = context.frame_index();
    region.dstBufferHandle = m_renderCellBuffer;
    region.byteSize = static_cast<uint64_t>(frameState.cellCount) *
                      sizeof(GpuData::RenderCellGpu);
    Result result = context.commandContext()->copy_buffer_region(region);
    if (result) {
      m_copyVersion.mark_copied();
    }
  }

private:
  const DrawFrameState &m_drawFrameState;
  RHI::BufferHandle m_renderCellBuffer{};
  mutable UploadCopyVersion m_copyVersion;
};
} // namespace Cue::DrawSystem
