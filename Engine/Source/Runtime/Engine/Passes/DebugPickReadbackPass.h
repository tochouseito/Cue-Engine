#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <GpuData/DebugPick.h>

namespace Cue
{
    class DebugPickReadbackPass final : public RHI::FrameGraphPass
    {
    public:
        DebugPickReadbackPass(
            GpuData::DebugPickState& a_pickState,
            RHI::BufferHandle a_readbackBufferHandle,
            uint32_t a_readbackDelayFrames) noexcept
            : m_pickState(a_pickState)
            , m_readbackBufferHandle(a_readbackBufferHandle)
            , m_readbackDelayFrames(a_readbackDelayFrames)
        {}

        const char* name() const noexcept override
        {
            return "DebugPickReadback";
        }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Graphics;
        }

        bool is_enabled(uint32_t) const noexcept override
        {
            return m_pickState.isRequested;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            return builder.get_texture("DebugObjectId", m_objectIdHandle);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            return builder.use_texture(
                m_objectIdHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::CopySource,
                RHI::ResourceState::ShaderResource);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr || !m_pickState.isRequested)
            {
                return;
            }

            RHI::TextureToBufferCopyRegion region{};
            region.srcTextureHandle = m_objectIdHandle;
            region.srcX = m_pickState.x;
            region.srcY = m_pickState.y;
            region.width = 1;
            region.height = 1;
            region.format = RHI::ColorFormat::R32_UINT;
            region.dstBufferHandle = m_readbackBufferHandle;
            region.dstReadbackResourceIndex = context.frame_index();
            region.dstByteOffset = 0;
            const Result result =
                commandContext->copy_texture_region_to_buffer(region);
            if (!result)
            {
                return;
            }

            m_pickState.isRequested = false;
            m_pickState.isInFlight = true;
            m_pickState.readbackResourceIndex = context.frame_index();
            m_pickState.framesUntilReadable = m_readbackDelayFrames;
        }

    private:
        GpuData::DebugPickState& m_pickState;
        RHI::BufferHandle m_readbackBufferHandle{};
        uint32_t m_readbackDelayFrames = 1;
        RHI::TextureHandle m_objectIdHandle{};
    };
} // namespace Cue
