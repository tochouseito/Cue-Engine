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
#include <atomic>

namespace Cue::DrawSystem {
class RenderableInfoCopyPass final : public RHI::FrameGraphPass {
public:
  RenderableInfoCopyPass(const DrawFrameState &drawFrameState,
                         RHI::BufferHandle renderableInfoBuffer,
                         const std::atomic<uint64_t> &uploadRevision)
      : m_drawFrameState(drawFrameState),
        m_renderableInfoBuffer(renderableInfoBuffer),
        m_uploadRevision(uploadRevision) {}

  const char *name() const noexcept override { return "RenderableInfoCopy"; }
  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Copy;
  }
  bool is_enabled(uint32_t frameIndex) const noexcept override {
    static_cast<void>(frameIndex);
    return m_uploadRevision.load(std::memory_order_acquire) !=
           m_lastCopiedRevision;
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
    const uint64_t revision =
        m_uploadRevision.load(std::memory_order_acquire);
    const DrawFrameData &frameState =
        m_drawFrameState.frame_state(context.frame_index());
    if (!m_renderableInfoBuffer.valid()) {
      return;
    }
    if (frameState.objectCount == 0) {
      m_lastCopiedRevision = revision;
      return;
    }

    RHI::BufferCopyRegion region{};
    region.srcBufferHandle = m_renderableInfoBuffer;
    region.srcUploadResourceIndex = context.frame_index();
    region.dstBufferHandle = m_renderableInfoBuffer;
    region.byteSize = static_cast<uint64_t>(frameState.objectCount) *
                      sizeof(GpuData::RenderableInfo);
    const Result copyResult =
        context.commandContext()->copy_buffer_region(region);
    if (copyResult) {
      m_lastCopiedRevision = revision;
    }
  }

private:
  const DrawFrameState &m_drawFrameState;
  RHI::BufferHandle m_renderableInfoBuffer{};
  const std::atomic<uint64_t> &m_uploadRevision;
  uint64_t m_lastCopiedRevision = 0;
};

class TransformBufferCopyPass final : public RHI::FrameGraphPass {
public:
  TransformBufferCopyPass(const DrawFrameState &drawFrameState,
                           RHI::BufferHandle transformBuffer,
                           const std::atomic<uint64_t> &uploadRevision)
      : m_drawFrameState(drawFrameState), m_transformBuffer(transformBuffer),
        m_uploadRevision(uploadRevision) {}

  const char *name() const noexcept override { return "TransformBufferCopy"; }
  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Copy;
  }
  bool is_enabled(uint32_t frameIndex) const noexcept override {
    static_cast<void>(frameIndex);
    return m_uploadRevision.load(std::memory_order_acquire) !=
           m_lastCopiedRevision;
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
    const uint64_t revision =
        m_uploadRevision.load(std::memory_order_acquire);
    const DrawFrameData &frameState =
        m_drawFrameState.frame_state(context.frame_index());
    if (!m_transformBuffer.valid()) {
      return;
    }
    if (frameState.objectCount == 0) {
      m_lastCopiedRevision = revision;
      return;
    }

    RHI::BufferCopyRegion region{};
    region.srcBufferHandle = m_transformBuffer;
    region.srcUploadResourceIndex = context.frame_index();
    region.dstBufferHandle = m_transformBuffer;
    region.byteSize = static_cast<uint64_t>(frameState.objectCount) *
                      sizeof(GpuData::ObjectTransformGpu);
    const Result copyResult =
        context.commandContext()->copy_buffer_region(region);
    if (copyResult) {
      m_lastCopiedRevision = revision;
    }
  }

private:
  const DrawFrameState &m_drawFrameState;
  RHI::BufferHandle m_transformBuffer{};
  const std::atomic<uint64_t> &m_uploadRevision;
  uint64_t m_lastCopiedRevision = 0;
};

class ViewProjectionCopyPass final : public RHI::FrameGraphPass {
public:
  explicit ViewProjectionCopyPass(RHI::BufferHandle viewProjectionBuffer)
      : m_viewProjectionBuffer(viewProjectionBuffer) {}

  const char *name() const noexcept override { return "ViewProjectionCopy"; }
  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Copy;
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
    (void)context.commandContext()->copy_buffer_region(region);
  }

private:
  RHI::BufferHandle m_viewProjectionBuffer{};
};

class MaterialBufferCopyPass final : public RHI::FrameGraphPass {
public:
  MaterialBufferCopyPass(RHI::BufferHandle materialBuffer,
                         const std::atomic<uint64_t> &uploadRevision)
      : m_materialBuffer(materialBuffer), m_uploadRevision(uploadRevision) {}

  const char *name() const noexcept override { return "MaterialBufferCopy"; }
  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Copy;
  }
  bool is_enabled(uint32_t frameIndex) const noexcept override {
    static_cast<void>(frameIndex);
    return m_uploadRevision.load(std::memory_order_acquire) !=
           m_lastCopiedRevision;
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
    const uint64_t revision =
        m_uploadRevision.load(std::memory_order_acquire);
    if (!m_materialBuffer.valid()) {
      return;
    }

    RHI::BufferCopyRegion region{};
    region.srcBufferHandle = m_materialBuffer;
    region.srcUploadResourceIndex = context.frame_index();
    region.dstBufferHandle = m_materialBuffer;
    region.byteSize = sizeof(GpuData::MaterialGpu);
    const Result copyResult =
        context.commandContext()->copy_buffer_region(region);
    if (copyResult) {
      m_lastCopiedRevision = revision;
    }
  }

private:
  RHI::BufferHandle m_materialBuffer{};
  const std::atomic<uint64_t> &m_uploadRevision;
  uint64_t m_lastCopiedRevision = 0;
};

class RenderCellCopyPass final : public RHI::FrameGraphPass {
public:
  RenderCellCopyPass(const DrawFrameState &drawFrameState,
                      RHI::BufferHandle renderCellBuffer,
                      const std::atomic<uint64_t> &uploadRevision)
      : m_drawFrameState(drawFrameState), m_renderCellBuffer(renderCellBuffer),
        m_uploadRevision(uploadRevision) {
  }

  const char *name() const noexcept override { return "RenderCellCopy"; }
  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Copy;
  }
  bool is_enabled(uint32_t frameIndex) const noexcept override {
    static_cast<void>(frameIndex);
    return m_uploadRevision.load(std::memory_order_acquire) !=
           m_lastCopiedRevision;
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
    const uint64_t revision =
        m_uploadRevision.load(std::memory_order_acquire);
    const DrawFrameData &frameState =
        m_drawFrameState.frame_state(context.frame_index());
    if (!m_renderCellBuffer.valid()) {
      return;
    }
    if (frameState.cellCount == 0) {
      m_lastCopiedRevision = revision;
      return;
    }

    RHI::BufferCopyRegion region{};
    region.srcBufferHandle = m_renderCellBuffer;
    region.srcUploadResourceIndex = context.frame_index();
    region.dstBufferHandle = m_renderCellBuffer;
    region.byteSize = static_cast<uint64_t>(frameState.cellCount) *
                      sizeof(GpuData::RenderCellGpu);
    const Result copyResult =
        context.commandContext()->copy_buffer_region(region);
    if (copyResult) {
      m_lastCopiedRevision = revision;
    }
  }

private:
  const DrawFrameState &m_drawFrameState;
  RHI::BufferHandle m_renderCellBuffer{};
  const std::atomic<uint64_t> &m_uploadRevision;
  uint64_t m_lastCopiedRevision = 0;
};
} // namespace Cue::DrawSystem
