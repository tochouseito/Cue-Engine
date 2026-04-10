#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <GpuData/ViewProjection.h>

namespace Cue
{
    class ViewProjectionCopyPass final : public RHI::FrameGraphPass
    {
    public:
        const char* name() const noexcept override { return "ViewProjectionCopy"; }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Copy;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            return builder.get_buffer("ViewProjectionBuffer", m_viewProjectionBufferHandle);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            if (!m_viewProjectionBufferHandle.valid())
            {
                return;
            }

            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            RHI::BufferCopyRegion region{};
            region.srcBufferHandle = m_viewProjectionBufferHandle;
            region.srcUploadResourceIndex = 0;
            region.srcByteOffset = 0;
            region.dstBufferHandle = m_viewProjectionBufferHandle;
            region.dstDefaultResourceIndex = 0;
            region.dstByteOffset = 0;
            region.byteSize = sizeof(GpuData::ViewProjectionGpu);

            RHI::ResourceBarrierDesc toCopyDestBarrier{};
            toCopyDestBarrier.after = RHI::ResourceState::CopyDest;
            if (!commandContext->resource_barrier(
                m_viewProjectionBufferHandle, toCopyDestBarrier))
            {
                return;
            }

            Result copyResult = commandContext->copy_buffer_region(region);

            RHI::ResourceBarrierDesc toCommonBarrier{};
            toCommonBarrier.after = RHI::ResourceState::Common;
            if (!commandContext->resource_barrier(
                m_viewProjectionBufferHandle, toCommonBarrier))
            {
                return;
            }

            (void)copyResult;
        }

    private:
        RHI::BufferHandle m_viewProjectionBufferHandle{};
    };
} // namespace Cue
