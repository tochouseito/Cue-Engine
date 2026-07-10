#include "DrawSceneUploadCopyPass.h"

// === Base includes ===
#include <CueAssert.h>

namespace Cue::DrawSystem
{
    namespace
    {
        [[nodiscard]] Result copy_buffer_if_needed(
            RHI::ICommandContext& a_commandContext,
            RHI::BufferHandle a_handle,
            uint32_t a_frameIndex,
            uint64_t a_byteSize)
        {
            if (!a_handle.valid() || a_byteSize == 0)
            {
                return Result::ok();
            }

            // 同一 handle の upload heap から default heap へ移す RHI 契約をここへ閉じ込める
            RHI::BufferCopyRegion region{};
            region.srcBufferHandle = a_handle;
            region.srcUploadResourceIndex = a_frameIndex;
            region.dstBufferHandle = a_handle;
            region.dstDefaultResourceIndex = 0;
            region.byteSize = a_byteSize;
            return a_commandContext.copy_buffer_region(region);
        }
    } // namespace

    DrawSceneUploadCopyPass::DrawSceneUploadCopyPass(DrawSceneResources& a_sceneResources)
        : m_sceneResources(a_sceneResources)
    {
    }

    DrawSceneUploadCopyPass::~DrawSceneUploadCopyPass() = default;

    const char* DrawSceneUploadCopyPass::name() const noexcept
    {
        return "DrawSceneUploadCopy";
    }

    RHI::CommandListType DrawSceneUploadCopyPass::type() const noexcept
    {
        return RHI::CommandListType::Copy;
    }

    Result DrawSceneUploadCopyPass::setup(RHI::FrameGraphBuilder& a_builder)
    {
        (void)a_builder;
        return Result::ok();
    }

    Result DrawSceneUploadCopyPass::describe_resources(RHI::FrameGraphBuilder& a_builder)
    {
        // shared Scene は Main graph で一度だけ転送し、Debug graph は読み取り状態のまま再利用する
        Result result = a_builder.use_buffer(
            m_sceneResources.renderable_info_buffer_handle(),
            RHI::ResourceAccessType::Write,
            RHI::ResourceState::CopyDest,
            RHI::ResourceState::CopyDest);
        if (!result)
        {
            return result;
        }
        return a_builder.use_buffer(
            m_sceneResources.transform_buffer_handle(),
            RHI::ResourceAccessType::Write,
            RHI::ResourceState::CopyDest,
            RHI::ResourceState::CopyDest);
    }

    void DrawSceneUploadCopyPass::execute(RHI::FrameGraphContext& a_context)
    {
        RHI::ICommandContext* commandContext = a_context.commandContext();
        if (commandContext == nullptr)
        {
            return;
        }

        Result result = copy_buffer_if_needed(
            *commandContext,
            m_sceneResources.renderable_info_buffer_handle(),
            a_context.frame_index(),
            m_sceneResources.renderable_info_buffer_byte_size());
        CUE_ASSERT_FORMAT(success(result), "Failed to copy RenderableInfoBuffer upload data: {}", result.message.data());

        result = copy_buffer_if_needed(
            *commandContext,
            m_sceneResources.transform_buffer_handle(),
            a_context.frame_index(),
            m_sceneResources.transform_buffer_byte_size());
        CUE_ASSERT_FORMAT(success(result), "Failed to copy TransformBuffer upload data: {}", result.message.data());
    }
} // namespace Cue::DrawSystem
