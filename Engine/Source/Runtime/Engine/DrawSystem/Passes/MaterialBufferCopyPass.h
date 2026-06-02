// MaterialBufferCopyPass の役割と公開要素を定義する

#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <GameCore/GameWorld.h>
#include <GpuData/Batching.h>

namespace Cue::DrawSystem
{
    class MaterialBufferCopyPass final : public RHI::FrameGraphPass
    {
    public:
        explicit MaterialBufferCopyPass(RHI::BufferHandle a_materialBufferHandle)
            : m_materialBufferHandle(a_materialBufferHandle)
        {}

        const char* name() const noexcept override { return "MaterialBufferCopy"; }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Copy;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            return builder.read_buffer(m_materialBufferHandle);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            return builder.use_buffer(
                m_materialBufferHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::CopyDest,
                RHI::ResourceState::Common);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            if (!m_materialBufferHandle.valid())
            {
                return;
            }

            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            RHI::BufferCopyRegion region{};
            region.srcBufferHandle = m_materialBufferHandle;
            region.srcUploadResourceIndex = context.frame_index();
            region.srcByteOffset = 0;
            region.dstBufferHandle = m_materialBufferHandle;
            region.dstDefaultResourceIndex = 0;
            region.dstByteOffset = 0;
            region.byteSize =
                static_cast<uint64_t>(GameCore::GameWorld::k_maxMaterialCount) *
                sizeof(GpuData::MaterialGpu);

            Result copyResult = commandContext->copy_buffer_region(region);

            (void)copyResult;
        }

    private:
        RHI::BufferHandle m_materialBufferHandle{};
    };
} // namespace Cue::DrawSystem
