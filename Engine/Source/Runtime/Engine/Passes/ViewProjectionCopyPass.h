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
        explicit ViewProjectionCopyPass(
            RHI::BufferHandle a_viewProjectionBufferHandle)
            : m_viewProjectionBufferHandle(a_viewProjectionBufferHandle)
        {}

        const char* name() const noexcept override { return "ViewProjectionCopy"; }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Copy;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            return builder.read_buffer(m_viewProjectionBufferHandle);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            return builder.use_buffer(
                m_viewProjectionBufferHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::CopyDest,
                RHI::ResourceState::Common);
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
            region.srcUploadResourceIndex = context.frame_index();
            region.srcByteOffset = 0;
            region.dstBufferHandle = m_viewProjectionBufferHandle;
            region.dstDefaultResourceIndex = 0;
            region.dstByteOffset = 0;
            region.byteSize = sizeof(GpuData::ViewProjectionGpu);

            Result copyResult = commandContext->copy_buffer_region(region);

            (void)copyResult;
        }

    private:
        RHI::BufferHandle m_viewProjectionBufferHandle{};
    };
} // namespace Cue
