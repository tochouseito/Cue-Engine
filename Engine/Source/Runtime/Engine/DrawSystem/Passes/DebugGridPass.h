// DebugGridPass の役割と公開要素を定義する

#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === C++ includes ===
#include <string>
#include <utility>

namespace Cue::DrawSystem
{
    class DebugGridPass final : public RHI::FrameGraphPass
    {
    public:
        DebugGridPass(
            RHI::BufferHandle a_viewProjectionBufferHandle,
            const bool& a_isVisible) noexcept
            : m_viewProjectionBufferHandle(a_viewProjectionBufferHandle)
            , m_isVisible(a_isVisible)
        {}

        DebugGridPass(
            std::string a_name,
            std::string a_colorName,
            std::string a_colorRtvName,
            std::string a_depthName,
            std::string a_depthDsvName,
            RHI::BufferHandle a_viewProjectionBufferHandle,
            const bool& a_isVisible) noexcept
            : m_name(std::move(a_name))
            , m_colorName(std::move(a_colorName))
            , m_colorRtvName(std::move(a_colorRtvName))
            , m_depthName(std::move(a_depthName))
            , m_depthDsvName(std::move(a_depthDsvName))
            , m_viewProjectionBufferHandle(a_viewProjectionBufferHandle)
            , m_isVisible(a_isVisible)
        {}

        const char* name() const noexcept override
        {
            return m_name.c_str();
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
            result = builder.get_view(m_depthDsvName, m_depthDsvHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_texture(m_depthName, m_depthHandle);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_viewProjectionBufferHandle);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = m_name + "RootSignature";
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::CBV,
                RHI::ShaderVisibility::All,
                0 });
            result = builder.create_root_signature(
                rootSignatureDesc, m_rootSignatureHandle);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc vertexShaderDesc{};
            vertexShaderDesc.name = m_name + "VS";
            vertexShaderDesc.filePath = "Shaders/D3D12/DebugGrid.hlsl";
            vertexShaderDesc.entryPoint = "vs_main";
            vertexShaderDesc.targetProfile = "vs_6_0";
            result = builder.create_shader_blob(
                vertexShaderDesc, m_vertexShaderHandle);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc pixelShaderDesc{};
            pixelShaderDesc.name = m_name + "PS";
            pixelShaderDesc.filePath = "Shaders/D3D12/DebugGrid.hlsl";
            pixelShaderDesc.entryPoint = "ps_main";
            pixelShaderDesc.targetProfile = "ps_6_0";
            result = builder.create_shader_blob(
                pixelShaderDesc, m_pixelShaderHandle);
            if (!result)
            {
                return result;
            }

            RHI::GraphicsPipelineStateDesc pipelineDesc{};
            pipelineDesc.name = m_name + "Pipeline";
            pipelineDesc.rootSignatureHandle = m_rootSignatureHandle;
            pipelineDesc.vsHandle = m_vertexShaderHandle;
            pipelineDesc.psHandle = m_pixelShaderHandle;
            pipelineDesc.rasterizerState.cullMode = RHI::CullMode::None;
            pipelineDesc.depthStencilState.depthEnable = true;
            pipelineDesc.depthStencilState.depthWriteMask =
                RHI::DepthWriteMask::Zero;
            pipelineDesc.depthStencilState.depthFunc =
                RHI::ComparisonFunc::LessEqual;
            pipelineDesc.dsvFormat = RHI::ColorFormat::D24_UNorm_S8_UInt;
            pipelineDesc.primitiveTopologyType =
                RHI::PrimitiveTopologyType::Line;
            pipelineDesc.blendMode = { RHI::BlendMode::Normal };
            pipelineDesc.rtvFormats = { RHI::ColorFormat::R8G8B8A8_UNORM };
            result = builder.create_graphics_pipeline(
                pipelineDesc, m_pipelineHandle);
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

            result = builder.use_texture(
                m_depthHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::DepthWrite,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }

            return builder.use_buffer(
                m_viewProjectionBufferHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::Common);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            if (!m_isVisible)
            {
                return;
            }

            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            commandContext->set_render_targets(
                &m_colorRtvHandle,
                1,
                m_depthDsvHandle);
            commandContext->set_viewport_scissor(context.width(), context.height());
            commandContext->set_graphics_pipeline(m_pipelineHandle);
            commandContext->set_primitive_topology(
                RHI::PrimitiveTopologyType::Line);
            commandContext->set_cbv(0, m_viewProjectionBufferHandle);
            commandContext->draw_instanced(k_vertexCount, 1, 0, 0);
        }

    private:
        static constexpr uint32_t k_halfGridLineCount = 50;
        static constexpr uint32_t k_lineCount =
            (k_halfGridLineCount * 2 + 1) * 2;
        static constexpr uint32_t k_vertexCount = k_lineCount * 2;

        std::string m_name = "DebugGrid";
        std::string m_colorName = "DebugColor";
        std::string m_colorRtvName = "DebugColorRTV";
        std::string m_depthName = "DebugSceneDepth";
        std::string m_depthDsvName = "DebugSceneDepthDSV";
        RHI::BufferHandle m_viewProjectionBufferHandle{};
        RHI::TextureHandle m_colorHandle{};
        RHI::TextureHandle m_depthHandle{};
        RHI::ViewHandle m_colorRtvHandle{};
        RHI::ViewHandle m_depthDsvHandle{};
        RHI::RootSignatureHandle m_rootSignatureHandle{};
        RHI::ShaderBlobHandle m_vertexShaderHandle{};
        RHI::ShaderBlobHandle m_pixelShaderHandle{};
        RHI::PipelineStateHandle m_pipelineHandle{};
        const bool& m_isVisible;
    };
} // namespace Cue::DrawSystem
