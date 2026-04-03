#pragma once

// === RHI includes ===
#include <FrameGraph.h>

namespace Cue
{
    class ObjectInfoCopyPass final : public RHI::FrameGraphPass
    {
    public:
        explicit ObjectInfoCopyPass(uint64_t a_copyByteSize)
            : m_copyByteSize(a_copyByteSize)
        {
        }

        const char* name() const noexcept override
        {
            return "ObjectInfoCopy";
        }

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
            if (m_hasCopied || !m_objectInfoBufferHandle.valid() || m_copyByteSize == 0)
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
            region.byteSize = m_copyByteSize;

            RHI::ResourceBarrierDesc toCopyDestBarrier{};
            toCopyDestBarrier.after = RHI::ResourceState::CopyDest;
            if (!commandContext->resource_barrier(m_objectInfoBufferHandle, toCopyDestBarrier))
            {
                return;
            }

            Result hasCopied = commandContext->copy_buffer_region(region);

            RHI::ResourceBarrierDesc toCommonBarrier{};
            toCommonBarrier.after = RHI::ResourceState::Common;
            if (!commandContext->resource_barrier(m_objectInfoBufferHandle, toCommonBarrier))
            {
                return;
            }

            if (hasCopied)
            {
                m_hasCopied = true;
            }
        }
    private:
        bool m_hasCopied = false;
        uint64_t m_copyByteSize = 0;
        RHI::BufferHandle m_objectInfoBufferHandle{};
    };
}
