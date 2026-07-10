#include "DrawVisibilityUploadCopyPass.h"

// === Base includes ===
#include <CueAssert.h>

namespace Cue::DrawSystem
{
    namespace
    {
        [[nodiscard]] Result copy_buffer(
            RHI::ICommandContext& a_commandContext,
            RHI::BufferHandle a_handle,
            uint32_t a_frameIndex,
            uint64_t a_byteSize)
        {
            // indirect argument は Graphics pass の直前に View 固有 default heap へ確定させる
            RHI::BufferCopyRegion region{};
            region.srcBufferHandle = a_handle;
            region.srcUploadResourceIndex = a_frameIndex;
            region.dstBufferHandle = a_handle;
            region.dstDefaultResourceIndex = 0;
            region.byteSize = a_byteSize;
            return a_commandContext.copy_buffer_region(region);
        }
    } // namespace

    DrawVisibilityUploadCopyPass::DrawVisibilityUploadCopyPass(DrawVisibilityResources& a_visibilityResources)
        : m_visibilityResources(a_visibilityResources)
    {
    }

    DrawVisibilityUploadCopyPass::~DrawVisibilityUploadCopyPass() = default;

    const char* DrawVisibilityUploadCopyPass::name() const noexcept
    {
        return "DrawVisibilityUploadCopy";
    }

    RHI::CommandListType DrawVisibilityUploadCopyPass::type() const noexcept
    {
        return RHI::CommandListType::Copy;
    }

    Result DrawVisibilityUploadCopyPass::setup(RHI::FrameGraphBuilder& a_builder)
    {
        (void)a_builder;
        return Result::ok();
    }

    Result DrawVisibilityUploadCopyPass::describe_resources(RHI::FrameGraphBuilder& a_builder)
    {
        // culling 導入後も同じ pass が CopyDest から Visibility 出力を供給できるよう、command 系を一括で宣言する
        Result result = a_builder.use_buffer(
            m_visibilityResources.static_mesh_indirect_command_buffer_handle(),
            RHI::ResourceAccessType::Write,
            RHI::ResourceState::CopyDest,
            RHI::ResourceState::CopyDest);
        if (!result)
        {
            return result;
        }
        result = a_builder.use_buffer(
            m_visibilityResources.static_mesh_indirect_command_count_buffer_handle(),
            RHI::ResourceAccessType::Write,
            RHI::ResourceState::CopyDest,
            RHI::ResourceState::CopyDest);
        if (!result)
        {
            return result;
        }
        return a_builder.use_buffer(
            m_visibilityResources.static_mesh_object_index_buffer_handle(),
            RHI::ResourceAccessType::Write,
            RHI::ResourceState::CopyDest,
            RHI::ResourceState::CopyDest);
    }

    void DrawVisibilityUploadCopyPass::execute(RHI::FrameGraphContext& a_context)
    {
        RHI::ICommandContext* commandContext = a_context.commandContext();
        if (commandContext == nullptr)
        {
            return;
        }

        const uint32_t frameIndex = a_context.frame_index();
        Result result = copy_buffer(
            *commandContext,
            m_visibilityResources.static_mesh_indirect_command_buffer_handle(),
            frameIndex,
            m_visibilityResources.static_mesh_indirect_command_buffer_byte_size());
        CUE_ASSERT_FORMAT(success(result), "Failed to copy StaticMeshIndirectCommandBuffer upload data: {}",
                          result.message.data());

        result = copy_buffer(
            *commandContext,
            m_visibilityResources.static_mesh_indirect_command_count_buffer_handle(),
            frameIndex,
            m_visibilityResources.static_mesh_indirect_command_count_buffer_byte_size());
        CUE_ASSERT_FORMAT(success(result), "Failed to copy StaticMeshIndirectCommandCountBuffer upload data: {}",
                          result.message.data());

        result = copy_buffer(
            *commandContext,
            m_visibilityResources.static_mesh_object_index_buffer_handle(),
            frameIndex,
            m_visibilityResources.static_mesh_object_index_buffer_byte_size());
        CUE_ASSERT_FORMAT(success(result), "Failed to copy StaticMeshObjectIndexBuffer upload data: {}",
                          result.message.data());
    }
} // namespace Cue::DrawSystem
