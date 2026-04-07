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
        explicit TransformBufferCopyPass(const RenderFrameState& a_frameState)
            : m_frameState(a_frameState)
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
            if (!m_transformBufferHandle.valid() || m_frameState.objectCount == 0)
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
            region.byteSize = static_cast<uint64_t>(m_frameState.objectCount) *
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
        const RenderFrameState& m_frameState;
        RHI::bufferHandle m_transformBufferHandle{};
    };
} // namespace Cue
