// ViewProjectionCopyPass の役割と公開要素を定義する

#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <GpuData/ViewProjection.h>

// === C++ includes ===
#include <string>
#include <string_view>

namespace Cue::DrawSystem
{
    class ViewProjectionCopyPass final : public RHI::FrameGraphPass
    {
    public:
        explicit ViewProjectionCopyPass(
            RHI::BufferHandle a_viewProjectionBufferHandle,
            uint64_t a_byteSize = sizeof(GpuData::ViewProjectionGpu),
            std::string_view a_name = "ViewProjectionCopy")
            : m_viewProjectionBufferHandle(a_viewProjectionBufferHandle)
            , m_byteSize(a_byteSize)
            , m_name(a_name)
        {}

        const char* name() const noexcept override { return m_name.c_str(); }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Copy;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            return builder.read_buffer(m_viewProjectionBufferHandle);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            return builder.use_buffer(
                m_viewProjectionBufferHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::CopyDest,
                RHI::ResourceState::Common);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            if (!m_viewProjectionBufferHandle.valid())
            {
                return;
            }

            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            RHI::BufferCopyRegion region{};
            region.srcBufferHandle = m_viewProjectionBufferHandle;
            region.srcUploadResourceIndex = context.frame_index();
            region.srcByteOffset = 0;
            region.dstBufferHandle = m_viewProjectionBufferHandle;
            region.dstDefaultResourceIndex = 0;
            region.dstByteOffset = 0;
            region.byteSize = m_byteSize;

            Result copyResult = commandContext->copy_buffer_region(region);

            (void)copyResult;
        }

    private:
        RHI::BufferHandle m_viewProjectionBufferHandle{};
        uint64_t m_byteSize = sizeof(GpuData::ViewProjectionGpu);
        std::string m_name{};
    };
} // namespace Cue::DrawSystem
