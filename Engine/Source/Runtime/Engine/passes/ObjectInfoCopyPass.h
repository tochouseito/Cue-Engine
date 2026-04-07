#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <GameCore/RenderSceneState.h>

namespace Cue
{
    class ObjectInfoCopyPass final : public RHI::FrameGraphPass
    {
    public:
        explicit ObjectInfoCopyPass(const RenderFrameState& a_frameState)
            : m_frameState(a_frameState)
        {}

        const char* name() const noexcept override { return "ObjectInfoCopy"; }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Copy;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            return builder.get_buffer("ObjectInfoBuffer", m_objectInfoBufferHandle);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            if (!m_objectInfoBufferHandle.valid() || m_frameState.objectCount == 0)
            {
                return;
            }

            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            RHI::BufferCopyRegion region{};
            region.srcBufferHandle = m_objectInfoBufferHandle;
            region.srcUploadResourceIndex = 0;
            region.srcByteOffset = 0;
            region.dstBufferHandle = m_objectInfoBufferHandle;
            region.dstDefaultResourceIndex = 0;
            region.dstByteOffset = 0;
            region.byteSize = static_cast<uint64_t>(m_frameState.objectCount) *
                sizeof(GpuData::ObjectInfo);

            RHI::ResourceBarrierDesc toCopyDestBarrier{};
            toCopyDestBarrier.after = RHI::ResourceState::CopyDest;
            if (!commandContext->resource_barrier(m_objectInfoBufferHandle,
                toCopyDestBarrier))
            {
                return;
            }

            Result copyResult = commandContext->copy_buffer_region(region);

            RHI::ResourceBarrierDesc toCommonBarrier{};
            toCommonBarrier.after = RHI::ResourceState::Common;
            if (!commandContext->resource_barrier(m_objectInfoBufferHandle,
                toCommonBarrier))
            {
                return;
            }

            (void)copyResult;
        }

    private:
        const RenderFrameState& m_frameState;
        RHI::bufferHandle m_objectInfoBufferHandle{};
    };
} // namespace Cue
