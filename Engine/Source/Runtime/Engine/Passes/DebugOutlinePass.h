#pragma once

// === RHI includes ===
#include <FrameGraph.h>

namespace Cue
{
    class DebugOutlinePass final : public RHI::FrameGraphPass
    {
    public:
        explicit DebugOutlinePass(const uint32_t& a_selectedObjectId) noexcept
            : m_selectedObjectId(a_selectedObjectId)
        {}

        const char* name() const noexcept override
        {
            return "DebugOutline";
        }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Graphics;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.get_texture("DebugColor", m_colorHandle);
            if (!result)
            {
                return result;
            }
            result = builder.render(&m_colorHandle, 1);
            if (!result)
            {
                return result;
            }
            result = builder.get_view("DebugColorRTV", m_colorRtvHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_texture(
                "DebugOutlineObjectId",
                m_objectIdHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_view(
                "DebugOutlineObjectIdSRV",
                m_objectIdSrvHandle);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "DebugOutlineRootSignature";
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::_32BitConstants,
                RHI::ShaderVisibility::All,
                0 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::DescriptorTableSRV,
                RHI::ShaderVisibility::Pixel,
                0,
                1,
                0 });
            result = builder.create_root_signature(
                rootSignatureDesc, m_rootSignatureHandle);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc vertexShaderDesc{};
            vertexShaderDesc.name = "DebugOutlineVS";
            vertexShaderDesc.filePath = "Shaders/D3D12/DebugOutline.hlsl";
            vertexShaderDesc.entryPoint = "vs_main";
            vertexShaderDesc.targetProfile = "vs_6_0";
            result = builder.create_shader_blob(
                vertexShaderDesc, m_vertexShaderHandle);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc pixelShaderDesc{};
            pixelShaderDesc.name = "DebugOutlinePS";
            pixelShaderDesc.filePath = "Shaders/D3D12/DebugOutline.hlsl";
            pixelShaderDesc.entryPoint = "ps_main";
            pixelShaderDesc.targetProfile = "ps_6_0";
            result = builder.create_shader_blob(
                pixelShaderDesc, m_pixelShaderHandle);
            if (!result)
            {
                return result;
            }

            RHI::GraphicsPipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "DebugOutlinePipeline";
            pipelineDesc.rootSignatureHandle = m_rootSignatureHandle;
            pipelineDesc.vsHandle = m_vertexShaderHandle;
            pipelineDesc.psHandle = m_pixelShaderHandle;
            pipelineDesc.rasterizerState.cullMode = RHI::CullMode::None;
            pipelineDesc.depthStencilState.depthEnable = false;
            pipelineDesc.depthStencilState.depthWriteMask =
                RHI::DepthWriteMask::Zero;
            pipelineDesc.blendMode = { RHI::BlendMode::Normal };
            pipelineDesc.rtvFormats = { RHI::ColorFormat::R8G8B8A8_UNORM };
            return builder.create_graphics_pipeline(pipelineDesc, m_pipelineHandle);
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
                m_objectIdHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr || m_selectedObjectId == 0)
            {
                return;
            }

            commandContext->set_render_targets(&m_colorRtvHandle, 1, {});
            commandContext->set_viewport_scissor(context.width(), context.height());
            commandContext->set_graphics_pipeline(m_pipelineHandle);
            commandContext->set_primitive_topology(
                RHI::PrimitiveTopologyType::Triangle);
            commandContext->set_32bit_constant(0, m_selectedObjectId);
            commandContext->set_graphics_descriptor_table(1, m_objectIdSrvHandle);
            commandContext->draw_instanced(3, 1, 0, 0);
        }

    private:
        const uint32_t& m_selectedObjectId;
        RHI::TextureHandle m_colorHandle{};
        RHI::TextureHandle m_objectIdHandle{};
        RHI::ViewHandle m_colorRtvHandle{};
        RHI::ViewHandle m_objectIdSrvHandle{};
        RHI::RootSignatureHandle m_rootSignatureHandle{};
        RHI::ShaderBlobHandle m_vertexShaderHandle{};
        RHI::ShaderBlobHandle m_pixelShaderHandle{};
        RHI::PipelineStateHandle m_pipelineHandle{};
    };
} // namespace Cue
