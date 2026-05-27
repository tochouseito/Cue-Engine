#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === C++ includes ===
#include <array>
#include <string>
#include <utility>

namespace Cue::DrawSystem
{
    class EffectPreviewClearPass final : public RHI::FrameGraphPass
    {
    public:
        static constexpr std::array<float, 4> k_clearColor = {
            0.10f,
            0.11f,
            0.13f,
            1.0f,
        };

        EffectPreviewClearPass(std::string a_colorName,
            std::string a_colorRtvName,
            std::string a_depthName,
            std::string a_depthDsvName)
            : m_colorName(std::move(a_colorName))
            , m_colorRtvName(std::move(a_colorRtvName))
            , m_depthName(std::move(a_depthName))
            , m_depthDsvName(std::move(a_depthDsvName))
        {}

        const char* name() const noexcept override
        {
            return "EffectPreviewClear";
        }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Graphics;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.get_texture(m_colorName, m_colorHandle);
            if (!result)
            {
                return result;
            }

            result = builder.render(&m_colorHandle, 1);
            if (!result)
            {
                return result;
            }

            result = builder.get_view(m_colorRtvName, m_colorRtvHandle);
            if (!result)
            {
                return result;
            }

            RHI::TextureDesc depthDesc{};
            depthDesc.name = m_depthName;
            depthDesc.bufferCount = 1;
            depthDesc.kind = RHI::TextureKind::DepthStencil;
            depthDesc.width = builder.width();
            depthDesc.height = builder.height();
            depthDesc.format = RHI::ColorFormat::D24_UNorm_S8_UInt;
            depthDesc.clearDepth = 1.0f;
            depthDesc.clearStencil = 0;
            result = builder.create_texture(depthDesc, m_depthHandle);
            if (!result)
            {
                return result;
            }

            RHI::ViewDesc depthDsvDesc{};
            depthDsvDesc.name = m_depthDsvName;
            depthDsvDesc.type = RHI::ViewType::DepthStencil;
            depthDsvDesc.bufferKind = RHI::BufferKind::Texture;
            depthDsvDesc.textureHandle = m_depthHandle;
            depthDsvDesc.colorFormat = RHI::ColorFormat::D24_UNorm_S8_UInt;
            result = builder.create_view(depthDsvDesc, m_depthDsvHandle);
            if (!result)
            {
                return result;
            }

            return Result::ok();
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.use_texture(
                m_colorHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::RenderTarget,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }

            return builder.use_texture(
                m_depthHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::DepthWrite,
                RHI::ResourceState::DepthWrite);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            commandContext->clear_render_target(
                m_colorRtvHandle,
                k_clearColor.data());
            commandContext->clear_depth_stencil(m_depthDsvHandle, 1.0f, 0);
        }

    private:
        std::string m_colorName{};
        std::string m_colorRtvName{};
        std::string m_depthName{};
        std::string m_depthDsvName{};
        RHI::TextureHandle m_colorHandle{};
        RHI::TextureHandle m_depthHandle{};
        RHI::ViewHandle m_colorRtvHandle{};
        RHI::ViewHandle m_depthDsvHandle{};
    };
} // namespace Cue::DrawSystem
