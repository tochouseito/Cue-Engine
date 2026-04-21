#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <GameCore/RenderSceneState.h>

namespace Cue
{
    class RenderableInfoCopyPass final : public RHI::FrameGraphPass
    {
    public:
        explicit RenderableInfoCopyPass(const RenderSceneState& a_renderSceneState)
            : m_renderSceneState(a_renderSceneState)
        {}

        const char* name() const noexcept override { return "RenderableInfoCopy"; }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Copy;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            return builder.get_buffer("RenderableInfoBuffer",
                m_renderableInfoBufferHandle);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            return builder.use_buffer(
                m_renderableInfoBufferHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::CopyDest,
                RHI::ResourceState::Common);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            const RenderFrameState& frameState =
                m_renderSceneState.frame_state(context.frame_index());
            if (!m_renderableInfoBufferHandle.valid() || frameState.objectCount == 0)
            {
                return;
            }

            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            RHI::BufferCopyRegion region{};
            region.srcBufferHandle = m_renderableInfoBufferHandle;
            region.srcUploadResourceIndex = 0;
            region.srcByteOffset = 0;
            region.dstBufferHandle = m_renderableInfoBufferHandle;
            region.dstDefaultResourceIndex = 0;
            region.dstByteOffset = 0;
            region.byteSize = static_cast<uint64_t>(frameState.objectCount) *
                sizeof(GpuData::RenderableInfo);

            Result copyResult = commandContext->copy_buffer_region(region);

            (void)copyResult;
        }

    private:
        const RenderSceneState& m_renderSceneState;
        RHI::BufferHandle m_renderableInfoBufferHandle{};
    };
} // namespace Cue
