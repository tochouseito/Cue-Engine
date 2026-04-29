#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <GameCore/GameWorld.h>
#include <GameCore/RenderSceneState.h>
#include <GpuData/Sprite.h>

namespace Cue
{
    class SpriteInstanceCopyPass final : public RHI::FrameGraphPass
    {
    public:
        SpriteInstanceCopyPass(const RenderSceneState& a_renderSceneState,
            RHI::BufferHandle a_spriteInstanceBufferHandle)
            : m_renderSceneState(a_renderSceneState)
            , m_spriteInstanceBufferHandle(a_spriteInstanceBufferHandle)
        {}

        const char* name() const noexcept override { return "SpriteInstanceCopy"; }

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

            return m_renderSceneState.frame_state(a_frameIndex).spriteCount > 0;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            return builder.read_buffer(m_spriteInstanceBufferHandle);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            return builder.use_buffer(
                m_spriteInstanceBufferHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::CopyDest,
                RHI::ResourceState::Common);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            const RenderFrameState& frameState =
                m_renderSceneState.frame_state(context.frame_index());
            if (!m_spriteInstanceBufferHandle.valid() ||
                frameState.spriteCount == 0)
            {
                return;
            }

            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            RHI::BufferCopyRegion region{};
            region.srcBufferHandle = m_spriteInstanceBufferHandle;
            region.srcUploadResourceIndex = context.frame_index();
            region.srcByteOffset = 0;
            region.dstBufferHandle = m_spriteInstanceBufferHandle;
            region.dstDefaultResourceIndex = 0;
            region.dstByteOffset = 0;
            region.byteSize = static_cast<uint64_t>(frameState.spriteCount) *
                sizeof(GpuData::SpriteInstanceGpu);

            Result copyResult = commandContext->copy_buffer_region(region);

            (void)copyResult;
        }

    private:
        const RenderSceneState& m_renderSceneState;
        RHI::BufferHandle m_spriteInstanceBufferHandle{};
    };
} // namespace Cue
