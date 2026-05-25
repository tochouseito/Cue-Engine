#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <DrawSystem/DrawFrameState.h>
#include <GpuData/Batching.h>

namespace Cue::DrawSystem
{
    class RenderObjectCopyPass final : public RHI::FrameGraphPass
    {
    public:
        RenderObjectCopyPass(const DrawFrameState& a_drawFrameState,
            RHI::BufferHandle a_renderObjectBufferHandle)
            : m_drawFrameState(a_drawFrameState)
            , m_renderObjectBufferHandle(a_renderObjectBufferHandle)
        {}

        const char* name() const noexcept override { return "RenderObjectCopy"; }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Copy;
        }

        bool is_enabled(uint32_t a_frameIndex) const noexcept override
        {
            if (a_frameIndex >= m_drawFrameState.frameStates.size())
            {
                return false;
            }

            const DrawFrameData& frameState =
                m_drawFrameState.frame_state(a_frameIndex);
            return frameState.useCpuBatching && frameState.objectCount > 0;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            return builder.read_buffer(m_renderObjectBufferHandle);
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
            const DrawFrameData& frameState =
                m_drawFrameState.frame_state(context.frame_index());
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
            region.srcUploadResourceIndex = context.frame_index();
            region.srcByteOffset = 0;
            region.dstBufferHandle = m_renderObjectBufferHandle;
            region.dstDefaultResourceIndex = 0;
            region.dstByteOffset = 0;
            region.byteSize = static_cast<uint64_t>(frameState.objectCount) *
                sizeof(GpuData::RenderObject);

            (void)commandContext->copy_buffer_region(region);
        }

    private:
        const DrawFrameState& m_drawFrameState;
        RHI::BufferHandle m_renderObjectBufferHandle{};
    };
} // namespace Cue::DrawSystem
