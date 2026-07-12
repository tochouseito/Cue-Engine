#include "RHIUtils.h"

namespace Cue::RHI
{
    Result create_render_target_resources(
        IRenderBackend& a_backend,
        std::string_view a_name,
        RHI::ColorFormat a_format,
        RenderTargetResources& a_outResources,
        const float* a_clearColor)
    {
        auto* textureManager = a_backend.get_texture_manager();
        auto* viewManager = a_backend.get_view_manager();
        if (textureManager == nullptr || viewManager == nullptr)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Fatal,
                "Failed to get texture or view manager for size dependent resources.");
        }

        if (a_outResources.colorHandle.valid() ||
            a_outResources.colorRtvHandle.valid() ||
            a_outResources.colorSrvHandle.valid())
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "Render target resources must be empty before creation.");
        }

        std::string colorName(a_name);
        RHI::TextureDesc colorDesc{};
        colorDesc.name = colorName;
        colorDesc.bufferCount = 1;
        colorDesc.kind = RHI::TextureKind::RenderTarget;
        colorDesc.width = a_backend.width();
        colorDesc.height = a_backend.height();
        colorDesc.format = a_format;
        if (a_clearColor != nullptr)
        {
            colorDesc.clearColor[0] = a_clearColor[0];
            colorDesc.clearColor[1] = a_clearColor[1];
            colorDesc.clearColor[2] = a_clearColor[2];
            colorDesc.clearColor[3] = a_clearColor[3];
        }
        else
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Fatal,
                "Clear color must be provided for render target resources.");
        }
        if (a_format == RHI::ColorFormat::R32_UINT)
        {
            colorDesc.clearColor[0] = 0.0f;
            colorDesc.clearColor[1] = 0.0f;
            colorDesc.clearColor[2] = 0.0f;
            colorDesc.clearColor[3] = 0.0f;
        }
        Result result =
            textureManager->create_texture(colorDesc, a_outResources.colorHandle);
        if (!result)
        {
            return result;
        }

        RHI::ViewDesc colorRtvDesc{};
        colorRtvDesc.name = colorName + "RTV";
        colorRtvDesc.type = RHI::ViewType::RenderTarget;
        colorRtvDesc.bufferKind = RHI::BufferKind::Texture;
        colorRtvDesc.textureHandle = a_outResources.colorHandle;
        colorRtvDesc.colorFormat = a_format;
        result = viewManager->create_view(
            colorRtvDesc, a_outResources.colorRtvHandle);
        if (!result)
        {
            Result rollbackResult =
                destroy_render_target_resources(a_backend, a_outResources);
            if (!rollbackResult)
            {
                return rollbackResult;
            }
            return result;
        }

        RHI::ViewDesc colorSrvDesc{};
        colorSrvDesc.name = colorName + "SRV";
        colorSrvDesc.type = RHI::ViewType::ShaderResourceTexture2D;
        colorSrvDesc.bufferKind = RHI::BufferKind::Texture;
        colorSrvDesc.textureHandle = a_outResources.colorHandle;
        colorSrvDesc.colorFormat = a_format;
        colorSrvDesc.mipLevels = 1;
        result = viewManager->create_view(
            colorSrvDesc, a_outResources.colorSrvHandle);
        if (!result)
        {
            Result rollbackResult =
                destroy_render_target_resources(a_backend, a_outResources);
            if (!rollbackResult)
            {
                return rollbackResult;
            }
            return result;
        }

        return Result::ok();
    }

    Result destroy_render_target_resources(
        IRenderBackend& a_backend,
        RenderTargetResources& a_resources)
    {
        auto* textureManager = a_backend.get_texture_manager();
        auto* viewManager = a_backend.get_view_manager();

        if (viewManager == nullptr &&
            (a_resources.colorSrvHandle.valid() ||
             a_resources.colorRtvHandle.valid()))
        {
            return Result::fail(
                Code::NotFound,
                Severity::Fatal,
                "Failed to get view manager for render target resource destruction.");
        }

        if (viewManager != nullptr)
        {
            if (a_resources.colorSrvHandle.valid())
            {
                Result result =
                    viewManager->destroy_view(a_resources.colorSrvHandle);
                if (!result)
                {
                    return result;
                }
                a_resources.colorSrvHandle = {};
            }

            if (a_resources.colorRtvHandle.valid())
            {
                Result result =
                    viewManager->destroy_view(a_resources.colorRtvHandle);
                if (!result)
                {
                    return result;
                }
                a_resources.colorRtvHandle = {};
            }
        }

        if (textureManager == nullptr && a_resources.colorHandle.valid())
        {
            return Result::fail(
                Code::NotFound,
                Severity::Fatal,
                "Failed to get texture manager for render target resource destruction.");
        }

        if (textureManager != nullptr && a_resources.colorHandle.valid())
        {
            Result result =
                textureManager->destroy_texture(a_resources.colorHandle);
            if (!result)
            {
                return result;
            }
            a_resources.colorHandle = {};
        }

        return Result::ok();
    }

    Result create_depth_stencil_resources(
        IRenderBackend& a_backend,
        std::string_view a_name,
        DepthStencilResources& a_outResources)
    {
        auto* textureManager = a_backend.get_texture_manager();
        auto* viewManager = a_backend.get_view_manager();
        if (textureManager == nullptr || viewManager == nullptr)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Fatal,
                "Failed to get texture or view manager for depth stencil resources.");
        }

        if (a_outResources.depthHandle.valid() || a_outResources.depthDsvHandle.valid())
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "Depth stencil resources must be empty before creation.");
        }

        std::string depthName(a_name);
        TextureDesc depthDesc{};
        depthDesc.name = depthName;
        depthDesc.bufferCount = 1;
        depthDesc.kind = TextureKind::DepthStencil;
        depthDesc.width = a_backend.width();
        depthDesc.height = a_backend.height();
        depthDesc.format = ColorFormat::D24_UNorm_S8_UInt;
        depthDesc.clearDepth = 1.0f;
        depthDesc.clearStencil = 0;

        Result result = textureManager->create_texture(depthDesc, a_outResources.depthHandle);
        if (!result)
        {
            return result;
        }

        ViewDesc depthDsvDesc{};
        depthDsvDesc.name = depthName + "DSV";
        depthDsvDesc.type = ViewType::DepthStencil;
        depthDsvDesc.bufferKind = BufferKind::Texture;
        depthDsvDesc.textureHandle = a_outResources.depthHandle;
        depthDsvDesc.colorFormat = depthDesc.format;
        result = viewManager->create_view(depthDsvDesc, a_outResources.depthDsvHandle);
        if (!result)
        {
            const Result rollbackResult = destroy_depth_stencil_resources(a_backend, a_outResources);
            return rollbackResult ? result : rollbackResult;
        }

        return Result::ok();
    }

    Result destroy_depth_stencil_resources(
        IRenderBackend& a_backend,
        DepthStencilResources& a_resources)
    {
        auto* textureManager = a_backend.get_texture_manager();
        auto* viewManager = a_backend.get_view_manager();

        if (viewManager == nullptr && a_resources.depthDsvHandle.valid())
        {
            return Result::fail(
                Code::NotFound,
                Severity::Fatal,
                "Failed to get view manager for depth stencil resource destruction.");
        }
        if (viewManager != nullptr && a_resources.depthDsvHandle.valid())
        {
            Result result = viewManager->destroy_view(a_resources.depthDsvHandle);
            if (!result)
            {
                return result;
            }
            a_resources.depthDsvHandle = {};
        }

        if (textureManager == nullptr && a_resources.depthHandle.valid())
        {
            return Result::fail(
                Code::NotFound,
                Severity::Fatal,
                "Failed to get texture manager for depth stencil resource destruction.");
        }
        if (textureManager != nullptr && a_resources.depthHandle.valid())
        {
            Result result = textureManager->destroy_texture(a_resources.depthHandle);
            if (!result)
            {
                return result;
            }
            a_resources.depthHandle = {};
        }

        return Result::ok();
    }
}
