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

namespace Cue::DrawSystem {
class RenderableInfoCopyPass final : public RHI::FrameGraphPass {
public:
  RenderableInfoCopyPass(const DrawFrameState &drawFrameState,
                         RHI::BufferHandle renderableInfoBuffer)
      : m_drawFrameState(drawFrameState),
        m_renderableInfoBuffer(renderableInfoBuffer) {}

  const char *name() const noexcept override { return "RenderableInfoCopy"; }
  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Copy;
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
    (void)context.commandContext()->copy_buffer_region(region);
  }

private:
  const DrawFrameState &m_drawFrameState;
  RHI::BufferHandle m_renderableInfoBuffer{};
};

class TransformBufferCopyPass final : public RHI::FrameGraphPass {
public:
  TransformBufferCopyPass(const DrawFrameState &drawFrameState,
                          RHI::BufferHandle transformBuffer)
      : m_drawFrameState(drawFrameState), m_transformBuffer(transformBuffer) {}

  const char *name() const noexcept override { return "TransformBufferCopy"; }
  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Copy;
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
    (void)context.commandContext()->copy_buffer_region(region);
  }

private:
  const DrawFrameState &m_drawFrameState;
  RHI::BufferHandle m_transformBuffer{};
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
  explicit MaterialBufferCopyPass(RHI::BufferHandle materialBuffer)
      : m_materialBuffer(materialBuffer) {}

  const char *name() const noexcept override { return "MaterialBufferCopy"; }
  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Copy;
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
    (void)context.commandContext()->copy_buffer_region(region);
  }

private:
  RHI::BufferHandle m_materialBuffer{};
};

class RenderCellCopyPass final : public RHI::FrameGraphPass {
public:
  RenderCellCopyPass(const DrawFrameState &drawFrameState,
                     RHI::BufferHandle renderCellBuffer)
      : m_drawFrameState(drawFrameState), m_renderCellBuffer(renderCellBuffer) {
  }

  const char *name() const noexcept override { return "RenderCellCopy"; }
  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Copy;
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
    (void)context.commandContext()->copy_buffer_region(region);
  }

private:
  const DrawFrameState &m_drawFrameState;
  RHI::BufferHandle m_renderCellBuffer{};
};
} // namespace Cue::DrawSystem
