#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <GameCore/RenderSceneState.h>

namespace Cue
{
    class TransformBufferCopyPass final : public RHI::FrameGraphPass
    {
    public:
        explicit TransformBufferCopyPass(const RenderSceneState& a_renderSceneState)
            : m_renderSceneState(a_renderSceneState)
        {}

        const char* name() const noexcept override { return "TransformBufferCopy"; }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Copy;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            return builder.get_buffer("TransformBuffer", m_transformBufferHandle);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            const RenderFrameState& frameState =
                m_renderSceneState.frame_state(context.frame_index());
            if (!m_transformBufferHandle.valid() || frameState.objectCount == 0)
            {
                return;
            }

            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            RHI::BufferCopyRegion region{};
            region.srcBufferHandle = m_transformBufferHandle;
            region.srcUploadResourceIndex = 0;
            region.srcByteOffset = 0;
            region.dstBufferHandle = m_transformBufferHandle;
            region.dstDefaultResourceIndex = 0;
            region.dstByteOffset = 0;
            region.byteSize = static_cast<uint64_t>(frameState.objectCount) *
                sizeof(GpuData::ObjectTransformGpu);

            RHI::ResourceBarrierDesc toCopyDestBarrier{};
            toCopyDestBarrier.after = RHI::ResourceState::CopyDest;
            if (!commandContext->resource_barrier(m_transformBufferHandle,
                toCopyDestBarrier))
            {
                return;
            }

            Result copyResult = commandContext->copy_buffer_region(region);

            RHI::ResourceBarrierDesc toCommonBarrier{};
            toCommonBarrier.after = RHI::ResourceState::Common;
            if (!commandContext->resource_barrier(m_transformBufferHandle,
                toCommonBarrier))
            {
                return;
            }

            (void)copyResult;
        }

    private:
        const RenderSceneState& m_renderSceneState;
        RHI::BufferHandle m_transformBufferHandle{};
    };
} // namespace Cue
