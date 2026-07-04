#include "DrawResourceUploadCopyPass.h"

// === Base includes ===
#include <CueAssert.h>

namespace Cue::DrawSystem
{
    namespace
    {
        [[nodiscard]] Result copy_buffer_if_needed(RHI::ICommandContext& a_commandContext,
                                                   RHI::BufferHandle a_handle,
                                                   uint32_t a_frameIndex,
                                                   uint64_t a_byteSize)
        {
            if (!a_handle.valid() || a_byteSize == 0)
            {
                return Result::ok();
            }

            RHI::BufferCopyRegion region{};
            region.srcBufferHandle = a_handle;
            region.srcUploadResourceIndex = a_frameIndex;
            region.srcByteOffset = 0;
            region.dstBufferHandle = a_handle;
            region.dstDefaultResourceIndex = 0;
            region.dstByteOffset = 0;
            region.byteSize = a_byteSize;
            return a_commandContext.copy_buffer_region(region);
        }
    } // namespace

    DrawResourceUploadCopyPass::DrawResourceUploadCopyPass(DrawResources& a_drawResources)
        : m_drawResources(a_drawResources)
    {
    }

    DrawResourceUploadCopyPass::~DrawResourceUploadCopyPass() = default;

    const char* DrawResourceUploadCopyPass::name() const noexcept
    {
        return "DrawResourceUploadCopy";
    }

    RHI::CommandListType DrawResourceUploadCopyPass::type() const noexcept
    {
        return RHI::CommandListType::Copy;
    }

    Result DrawResourceUploadCopyPass::setup(RHI::FrameGraphBuilder& a_builder)
    {
        (void)a_builder;
        return Result::ok();
    }

    Result DrawResourceUploadCopyPass::describe_resources(RHI::FrameGraphBuilder& a_builder)
    {
        // CopyPass は default heap 側を CopyDest に遷移し、後続 pass が必要 state へ戻す。
        Result result = a_builder.use_buffer(m_drawResources.renderable_info_buffer_handle(),
                                             RHI::ResourceAccessType::Write,
                                             RHI::ResourceState::CopyDest,
                                             RHI::ResourceState::CopyDest);
        if (!result)
        {
            return result;
        }

        result = a_builder.use_buffer(m_drawResources.transform_buffer_handle(),
                                      RHI::ResourceAccessType::Write,
                                      RHI::ResourceState::CopyDest,
                                      RHI::ResourceState::CopyDest);
        if (!result)
        {
            return result;
        }

        result = a_builder.use_buffer(m_drawResources.view_projection_buffer_handle(),
                                      RHI::ResourceAccessType::Write,
                                      RHI::ResourceState::CopyDest,
                                      RHI::ResourceState::CopyDest);
        if (!result)
        {
            return result;
        }

        return a_builder.use_buffer(m_drawResources.particle_sprite_buffer_handle(),
                                    RHI::ResourceAccessType::Write,
                                    RHI::ResourceState::CopyDest,
                                    RHI::ResourceState::CopyDest);
    }

    void DrawResourceUploadCopyPass::execute(RHI::FrameGraphContext& a_context)
    {
        RHI::ICommandContext* commandContext = a_context.commandContext();
        if (commandContext == nullptr)
        {
            return;
        }

        const uint32_t frameIndex = a_context.frame_index();
        Result result = copy_buffer_if_needed(*commandContext,
                                              m_drawResources.renderable_info_buffer_handle(),
                                              frameIndex,
                                              m_drawResources.renderable_info_buffer_byte_size());
        CUE_ASSERT_FORMAT(success(result), "Failed to copy RenderableInfoBuffer upload data: {}",
                          result.message.data());

        result = copy_buffer_if_needed(*commandContext,
                                       m_drawResources.transform_buffer_handle(),
                                       frameIndex,
                                       m_drawResources.transform_buffer_byte_size());
        CUE_ASSERT_FORMAT(success(result), "Failed to copy TransformBuffer upload data: {}", result.message.data());

        result = copy_buffer_if_needed(*commandContext,
                                       m_drawResources.view_projection_buffer_handle(),
                                       frameIndex,
                                       m_drawResources.view_projection_buffer_byte_size());
        CUE_ASSERT_FORMAT(success(result), "Failed to copy ViewProjectionBuffer upload data: {}",
                          result.message.data());

        result = copy_buffer_if_needed(*commandContext,
                                       m_drawResources.particle_sprite_buffer_handle(),
                                       frameIndex,
                                       m_drawResources.particle_sprite_buffer_byte_size());
        CUE_ASSERT_FORMAT(success(result), "Failed to copy ParticleSpriteBuffer upload data: {}",
                          result.message.data());
    }
} // namespace Cue::DrawSystem
