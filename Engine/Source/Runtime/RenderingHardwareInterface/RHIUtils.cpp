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

        std::string colorName(a_name);
        RHI::TextureDesc colorDesc{};
        colorDesc.name = colorName;
        colorDesc.bufferCount = 1;
        colorDesc.kind = RHI::TextureKind::RenderTarget;
        colorDesc.width = a_backend.width();
        colorDesc.height = a_backend.height();
        colorDesc.format = a_format;
        Math::float4 clearColor = Math::float4::from_rgba8(63, 63, 63, 255);
        if (a_clearColor != nullptr)
        {
            colorDesc.clearColor[0] = a_clearColor[0];
            colorDesc.clearColor[1] = a_clearColor[1];
            colorDesc.clearColor[2] = a_clearColor[2];
            colorDesc.clearColor[3] = a_clearColor[3];
        }
        else
        {
            colorDesc.clearColor[0] = clearColor.r;
            colorDesc.clearColor[1] = clearColor.g;
            colorDesc.clearColor[2] = clearColor.b;
            colorDesc.clearColor[3] = clearColor.a;
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
}
