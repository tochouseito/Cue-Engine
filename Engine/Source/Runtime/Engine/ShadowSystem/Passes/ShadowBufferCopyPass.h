// ShadowBufferCopyPass の役割と公開要素を定義する

#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === C++ includes ===
#include <string>
#include <utility>

namespace Cue::ShadowSystem
{
    class ShadowBufferCopyPass final : public RHI::FrameGraphPass
    {
    public:
        ShadowBufferCopyPass(
            std::string a_name,
            RHI::BufferHandle a_bufferHandle,
            uint64_t a_copyByteSize)
            : m_name(std::move(a_name))
            , m_bufferHandle(a_bufferHandle)
            , m_copyByteSize(a_copyByteSize)
        {}

        const char* name() const noexcept override { return m_name.c_str(); }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Copy;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            return builder.read_buffer(m_bufferHandle);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            return builder.use_buffer(
                m_bufferHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::CopyDest,
                RHI::ResourceState::Common);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            if (!m_bufferHandle.valid() || m_copyByteSize == 0)
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
            region.srcUploadResourceIndex = context.frame_index();
            region.srcByteOffset = 0;
            region.dstBufferHandle = m_bufferHandle;
            region.dstDefaultResourceIndex = 0;
            region.dstByteOffset = 0;
            region.byteSize = m_copyByteSize;
            (void)commandContext->copy_buffer_region(region);
        }

    private:
        std::string m_name{};
        RHI::BufferHandle m_bufferHandle{};
        uint64_t m_copyByteSize = 0;
    };
}
