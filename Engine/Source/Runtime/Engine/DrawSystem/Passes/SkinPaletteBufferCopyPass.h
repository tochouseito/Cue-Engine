#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <GameCore/GameWorld.h>
#include <GpuData/Batching.h>

namespace Cue::DrawSystem
{
    class SkinPaletteBufferCopyPass final : public RHI::FrameGraphPass
    {
    public:
        explicit SkinPaletteBufferCopyPass(RHI::BufferHandle a_bufferHandle)
            : m_bufferHandle(a_bufferHandle)
        {
        }

        const char* name() const noexcept override
        {
            return "SkinPaletteBufferCopy";
        }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Copy;
        }

        Result setup(RHI::FrameGraphBuilder& a_builder) override
        {
            return a_builder.read_buffer(m_bufferHandle);
        }

        Result describe_resources(RHI::FrameGraphBuilder& a_builder) override
        {
            return a_builder.use_buffer(m_bufferHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::CopyDest,
                RHI::ResourceState::Common);
        }

        void execute(RHI::FrameGraphContext& a_context) override
        {
            if (!m_bufferHandle.valid())
            {
                return;
            }

            RHI::ICommandContext* commandContext = a_context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            RHI::BufferCopyRegion region{};
            region.srcBufferHandle = m_bufferHandle;
            region.srcUploadResourceIndex = a_context.frame_index();
            region.srcByteOffset = 0;
            region.dstBufferHandle = m_bufferHandle;
            region.dstDefaultResourceIndex = 0;
            region.dstByteOffset = 0;
            region.byteSize =
                static_cast<uint64_t>(GameCore::GameWorld::k_maxSkinPaletteCount) *
                sizeof(GpuData::SkinPaletteGpu);
            (void)commandContext->copy_buffer_region(region);
        }

    private:
        RHI::BufferHandle m_bufferHandle{};
    };
}
