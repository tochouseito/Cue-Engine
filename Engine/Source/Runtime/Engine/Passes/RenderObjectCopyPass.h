#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <GameCore/RenderSceneState.h>
#include <GpuData/Batching.h>

namespace Cue
{
    class RenderObjectCopyPass final : public RHI::FrameGraphPass
    {
    public:
        explicit RenderObjectCopyPass(const RenderSceneState& a_renderSceneState)
            : m_renderSceneState(a_renderSceneState)
        {}

        const char* name() const noexcept override { return "RenderObjectCopy"; }

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

            const RenderFrameState& frameState =
                m_renderSceneState.frame_state(a_frameIndex);
            return frameState.useCpuBatching && frameState.objectCount > 0;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            return builder.get_buffer("RenderObjectBuffer", m_renderObjectBufferHandle);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            return builder.use_buffer(
                m_renderObjectBufferHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::CopyDest,
                RHI::ResourceState::Common);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            const RenderFrameState& frameState =
                m_renderSceneState.frame_state(context.frame_index());
            if (!frameState.useCpuBatching ||
                !m_renderObjectBufferHandle.valid() ||
                frameState.objectCount == 0)
            {
                return;
            }

            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            RHI::BufferCopyRegion region{};
            region.srcBufferHandle = m_renderObjectBufferHandle;
            region.srcUploadResourceIndex = 0;
            region.srcByteOffset = 0;
            region.dstBufferHandle = m_renderObjectBufferHandle;
            region.dstDefaultResourceIndex = 0;
            region.dstByteOffset = 0;
            region.byteSize = static_cast<uint64_t>(frameState.objectCount) *
                sizeof(GpuData::RenderObject);

            (void)commandContext->copy_buffer_region(region);
        }

    private:
        const RenderSceneState& m_renderSceneState;
        RHI::BufferHandle m_renderObjectBufferHandle{};
    };
} // namespace Cue
