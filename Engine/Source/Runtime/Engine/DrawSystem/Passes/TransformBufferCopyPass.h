#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <DrawSystem/DrawFrameState.h>

namespace Cue::DrawSystem
{
    class TransformBufferCopyPass final : public RHI::FrameGraphPass
    {
    public:
        TransformBufferCopyPass(const DrawFrameState& a_drawFrameState,
            RHI::BufferHandle a_transformBufferHandle)
            : m_drawFrameState(a_drawFrameState)
            , m_transformBufferHandle(a_transformBufferHandle)
        {}

        const char* name() const noexcept override { return "TransformBufferCopy"; }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Copy;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            return builder.read_buffer(m_transformBufferHandle);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            return builder.use_buffer(
                m_transformBufferHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::CopyDest,
                RHI::ResourceState::Common);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            const DrawFrameData& frameState =
                m_drawFrameState.frame_state(context.frame_index());
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
            region.srcUploadResourceIndex = context.frame_index();
            region.srcByteOffset = 0;
            region.dstBufferHandle = m_transformBufferHandle;
            region.dstDefaultResourceIndex = 0;
            region.dstByteOffset = 0;
            region.byteSize = static_cast<uint64_t>(frameState.objectCount) *
                sizeof(GpuData::ObjectTransformGpu);

            Result copyResult = commandContext->copy_buffer_region(region);

            (void)copyResult;
        }

    private:
        const DrawFrameState& m_drawFrameState;
        RHI::BufferHandle m_transformBufferHandle{};
    };
} // namespace Cue::DrawSystem
