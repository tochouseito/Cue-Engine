// EffectPrimitiveBufferCopyPass の役割と公開要素を定義する

#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <EffectSystem/EffectPrimitiveFrameState.h>
#include <GpuData/Effect.h>

namespace Cue::EffectSystem {
class EffectPrimitiveBufferCopyPass final : public RHI::FrameGraphPass {
public:
  EffectPrimitiveBufferCopyPass(const EffectPrimitiveFrameState &a_frameState,
                                RHI::BufferHandle a_frameBufferHandle,
                                RHI::BufferHandle a_spriteBufferHandle,
                                RHI::BufferHandle a_ribbonBufferHandle)
      : m_frameState(a_frameState), m_frameBufferHandle(a_frameBufferHandle),
        m_spriteBufferHandle(a_spriteBufferHandle),
        m_ribbonBufferHandle(a_ribbonBufferHandle) {}

  const char *name() const noexcept override {
    return "EffectPrimitiveBufferCopy";
  }

  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Copy;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.read_buffer(m_frameBufferHandle);
    if (!result) {
      return result;
    }

    result = builder.read_buffer(m_spriteBufferHandle);
    if (!result) {
      return result;
    }

    return builder.read_buffer(m_ribbonBufferHandle);
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.use_buffer(
        m_frameBufferHandle, RHI::ResourceAccessType::Write,
        RHI::ResourceState::CopyDest, RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }

    result = builder.use_buffer(
        m_spriteBufferHandle, RHI::ResourceAccessType::Write,
        RHI::ResourceState::CopyDest, RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }

    return builder.use_buffer(
        m_ribbonBufferHandle, RHI::ResourceAccessType::Write,
        RHI::ResourceState::CopyDest, RHI::ResourceState::ShaderResource);
  }

  void execute(RHI::FrameGraphContext &context) override {
    RHI::ICommandContext *commandContext = context.commandContext();
    if (commandContext == nullptr) {
      return;
    }

    const EffectPrimitiveFrameData &frameState =
        m_frameState.frame_state(context.frame_index());

    RHI::BufferCopyRegion frameRegion{};
    frameRegion.srcBufferHandle = m_frameBufferHandle;
    frameRegion.srcUploadResourceIndex = context.frame_index();
    frameRegion.dstBufferHandle = m_frameBufferHandle;
    frameRegion.dstDefaultResourceIndex = 0;
    frameRegion.byteSize = sizeof(GpuData::EffectFrameGpu);
    (void)commandContext->copy_buffer_region(frameRegion);

    if (frameState.frame.spriteCount > 0) {
      RHI::BufferCopyRegion spriteRegion{};
      spriteRegion.srcBufferHandle = m_spriteBufferHandle;
      spriteRegion.srcUploadResourceIndex = context.frame_index();
      spriteRegion.dstBufferHandle = m_spriteBufferHandle;
      spriteRegion.dstDefaultResourceIndex = 0;
      spriteRegion.byteSize =
          static_cast<uint64_t>(frameState.frame.spriteCount) *
          sizeof(GpuData::EffectSpriteGpu);
      (void)commandContext->copy_buffer_region(spriteRegion);
    }

    if (frameState.frame.ribbonCount > 0) {
      RHI::BufferCopyRegion ribbonRegion{};
      ribbonRegion.srcBufferHandle = m_ribbonBufferHandle;
      ribbonRegion.srcUploadResourceIndex = context.frame_index();
      ribbonRegion.dstBufferHandle = m_ribbonBufferHandle;
      ribbonRegion.dstDefaultResourceIndex = 0;
      ribbonRegion.byteSize =
          static_cast<uint64_t>(frameState.frame.ribbonCount) *
          sizeof(GpuData::EffectRibbonGpu);
      (void)commandContext->copy_buffer_region(ribbonRegion);
    }
  }

private:
  const EffectPrimitiveFrameState &m_frameState;
  RHI::BufferHandle m_frameBufferHandle{};
  RHI::BufferHandle m_spriteBufferHandle{};
  RHI::BufferHandle m_ribbonBufferHandle{};
};
} // namespace Cue::EffectSystem
