// RenderableInfoCopyPass の役割と公開要素を定義する

#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <DrawSystem/DrawFrameState.h>

namespace Cue::DrawSystem
{
    class RenderableInfoCopyPass final : public RHI::FrameGraphPass
    {
    public:
        RenderableInfoCopyPass(const DrawFrameState& a_drawFrameState,
            RHI::BufferHandle a_renderableInfoBufferHandle)
            : m_drawFrameState(a_drawFrameState)
            , m_renderableInfoBufferHandle(a_renderableInfoBufferHandle)
        {}

        const char* name() const noexcept override { return "RenderableInfoCopy"; }

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

            return !m_drawFrameState.frame_state(a_frameIndex).useCpuBatching;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            return builder.read_buffer(
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
            const DrawFrameData& frameState =
                m_drawFrameState.frame_state(context.frame_index());
            if (frameState.useCpuBatching ||
                !m_renderableInfoBufferHandle.valid() ||
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
            region.srcBufferHandle = m_renderableInfoBufferHandle;
            region.srcUploadResourceIndex = context.frame_index();
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
        const DrawFrameState& m_drawFrameState;
        RHI::BufferHandle m_renderableInfoBufferHandle{};
    };
} // namespace Cue::DrawSystem
