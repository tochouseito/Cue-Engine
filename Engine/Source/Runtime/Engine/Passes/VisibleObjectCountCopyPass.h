#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <GameCore/RenderSceneState.h>

namespace Cue
{
    class VisibleObjectCountCopyPass final : public RHI::FrameGraphPass
    {
    public:
        VisibleObjectCountCopyPass(const RenderSceneState& a_renderSceneState,
            RHI::BufferHandle a_visibleObjectCountBufferHandle)
            : m_renderSceneState(a_renderSceneState)
            , m_visibleObjectCountBufferHandle(a_visibleObjectCountBufferHandle)
        {}

        const char* name() const noexcept override
        {
            return "VisibleObjectCountCopy";
        }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Copy;
        }

        bool is_enabled(uint32_t a_frameIndex) const noexcept override
        {
            if (a_frameIndex >= m_renderSceneState.frameStates.size())
            {
                return false;
            }

            return m_renderSceneState.frame_state(a_frameIndex).useCpuBatching;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            return builder.read_buffer(m_visibleObjectCountBufferHandle);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            return builder.use_buffer(
                m_visibleObjectCountBufferHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::CopyDest,
                RHI::ResourceState::Common);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            const RenderFrameState& frameState =
                m_renderSceneState.frame_state(context.frame_index());
            if (!frameState.useCpuBatching || !m_visibleObjectCountBufferHandle.valid())
            {
                return;
            }

            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            RHI::BufferCopyRegion region{};
            region.srcBufferHandle = m_visibleObjectCountBufferHandle;
            region.srcUploadResourceIndex = 0;
            region.srcByteOffset = 0;
            region.dstBufferHandle = m_visibleObjectCountBufferHandle;
            region.dstDefaultResourceIndex = 0;
            region.dstByteOffset = 0;
            region.byteSize = sizeof(uint32_t);

            (void)commandContext->copy_buffer_region(region);
        }

    private:
        const RenderSceneState& m_renderSceneState;
        RHI::BufferHandle m_visibleObjectCountBufferHandle{};
    };
} // namespace Cue
