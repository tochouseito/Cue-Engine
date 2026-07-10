#include "DrawViewUploadCopyPass.h"

// === Base includes ===
#include <CueAssert.h>

namespace Cue::DrawSystem
{
    DrawViewUploadCopyPass::DrawViewUploadCopyPass(DrawViewResources& a_viewResources)
        : m_viewResources(a_viewResources)
    {
    }

    DrawViewUploadCopyPass::~DrawViewUploadCopyPass() = default;

    const char* DrawViewUploadCopyPass::name() const noexcept
    {
        return "DrawViewUploadCopy";
    }

    RHI::CommandListType DrawViewUploadCopyPass::type() const noexcept
    {
        return RHI::CommandListType::Copy;
    }

    Result DrawViewUploadCopyPass::setup(RHI::FrameGraphBuilder& a_builder)
    {
        (void)a_builder;
        return Result::ok();
    }

    Result DrawViewUploadCopyPass::describe_resources(RHI::FrameGraphBuilder& a_builder)
    {
        // camera 定数は View ごとに異なるため、Scene copy pass から独立して CopyDest を宣言する
        return a_builder.use_buffer(
            m_viewResources.view_projection_buffer_handle(),
            RHI::ResourceAccessType::Write,
            RHI::ResourceState::CopyDest,
            RHI::ResourceState::CopyDest);
    }

    void DrawViewUploadCopyPass::execute(RHI::FrameGraphContext& a_context)
    {
        RHI::ICommandContext* commandContext = a_context.commandContext();
        if (commandContext == nullptr)
        {
            return;
        }

        // frame resource と default resource の index を混同しないよう、ViewProjection 専用の copy region を構築する
        RHI::BufferCopyRegion region{};
        region.srcBufferHandle = m_viewResources.view_projection_buffer_handle();
        region.srcUploadResourceIndex = a_context.frame_index();
        region.dstBufferHandle = m_viewResources.view_projection_buffer_handle();
        region.dstDefaultResourceIndex = 0;
        region.byteSize = m_viewResources.view_projection_buffer_byte_size();
        Result result = commandContext->copy_buffer_region(region);
        CUE_ASSERT_FORMAT(success(result), "Failed to copy ViewProjectionBuffer upload data: {}", result.message.data());
    }
} // namespace Cue::DrawSystem
