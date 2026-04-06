#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === C++ includes ===
#include <string>
#include <string_view>

namespace Cue
{
    class UploadBufferCopyPass final : public RHI::FrameGraphPass
    {
    public:
        UploadBufferCopyPass(std::string_view a_bufferName, uint64_t a_copyByteSize)
            : m_bufferName(a_bufferName)
            , m_passName(std::string(a_bufferName) + "Copy")
            , m_copyByteSize(a_copyByteSize)
        {
        }

        const char* name() const noexcept override
        {
            return m_passName.c_str();
        }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Copy;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            return builder.get_buffer(m_bufferName, m_bufferHandle);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            if (m_hasCopied || !m_bufferHandle.valid() || m_copyByteSize == 0)
            {
                return;
            }

            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            RHI::BufferCopyRegion region{};
            region.srcBufferHandle = m_bufferHandle;
            region.srcUploadResourceIndex = 0;
            region.srcByteOffset = 0;
            region.dstBufferHandle = m_bufferHandle;
            region.dstDefaultResourceIndex = 0;
            region.dstByteOffset = 0;
            region.byteSize = m_copyByteSize;

            RHI::ResourceBarrierDesc toCopyDestBarrier{};
            toCopyDestBarrier.after = RHI::ResourceState::CopyDest;
            if (!commandContext->resource_barrier(m_bufferHandle, toCopyDestBarrier))
            {
                return;
            }

            Result hasCopied = commandContext->copy_buffer_region(region);

            RHI::ResourceBarrierDesc toCommonBarrier{};
            toCommonBarrier.after = RHI::ResourceState::Common;
            if (!commandContext->resource_barrier(m_bufferHandle, toCommonBarrier))
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
        std::string m_bufferName{};
        std::string m_passName{};
        uint64_t m_copyByteSize = 0;
        RHI::bufferHandle m_bufferHandle{};
    };
}
