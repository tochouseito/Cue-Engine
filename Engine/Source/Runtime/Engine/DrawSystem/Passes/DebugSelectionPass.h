// DebugSelectionPass の役割と公開要素を定義する

#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <GpuData/DebugSelection.h>

namespace Cue::DrawSystem
{
    class DebugSelectionPass final : public RHI::FrameGraphPass
    {
    public:
        DebugSelectionPass(
            RHI::BufferHandle a_viewProjectionBufferHandle,
            RHI::BufferHandle a_selectionBufferHandle)
            : m_viewProjectionBufferHandle(a_viewProjectionBufferHandle)
            , m_selectionBufferHandle(a_selectionBufferHandle)
        {}

        const char* name() const noexcept override
        {
            return "DebugSelection";
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
            result = builder.read_buffer(m_viewProjectionBufferHandle);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_selectionBufferHandle);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "DebugSelectionRootSignature";
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::CBV,
                RHI::ShaderVisibility::All,
                0 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::CBV,
                RHI::ShaderVisibility::All,
                1 });
            result = builder.create_root_signature(
                rootSignatureDesc, m_rootSignatureHandle);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc vertexShaderDesc{};
            vertexShaderDesc.name = "DebugSelectionVS";
            vertexShaderDesc.filePath = "Shaders/D3D12/DebugSelection.hlsl";
            vertexShaderDesc.entryPoint = "vs_main";
            vertexShaderDesc.targetProfile = "vs_6_0";
            result = builder.create_shader_blob(
                vertexShaderDesc, m_vertexShaderHandle);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc pixelShaderDesc{};
            pixelShaderDesc.name = "DebugSelectionPS";
            pixelShaderDesc.filePath = "Shaders/D3D12/DebugSelection.hlsl";
            pixelShaderDesc.entryPoint = "ps_main";
            pixelShaderDesc.targetProfile = "ps_6_0";
            result = builder.create_shader_blob(
                pixelShaderDesc, m_pixelShaderHandle);
            if (!result)
            {
                return result;
            }

            RHI::GraphicsPipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "DebugSelectionPipeline";
            pipelineDesc.rootSignatureHandle = m_rootSignatureHandle;
            pipelineDesc.vsHandle = m_vertexShaderHandle;
            pipelineDesc.psHandle = m_pixelShaderHandle;
            pipelineDesc.rasterizerState.cullMode = RHI::CullMode::None;
            pipelineDesc.depthStencilState.depthEnable = false;
            pipelineDesc.depthStencilState.depthWriteMask =
                RHI::DepthWriteMask::Zero;
            pipelineDesc.primitiveTopologyType =
                RHI::PrimitiveTopologyType::Line;
            pipelineDesc.blendMode = { RHI::BlendMode::Normal };
            pipelineDesc.rtvFormats = { RHI::ColorFormat::R8G8B8A8_UNORM };
            return builder.create_graphics_pipeline(
                pipelineDesc, m_pipelineHandle);
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

            result = builder.use_buffer(
                m_viewProjectionBufferHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }

            return builder.use_buffer(
                m_selectionBufferHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::Common);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            commandContext->set_render_targets(&m_colorRtvHandle, 1, {});
            commandContext->set_viewport_scissor(context.width(), context.height());
            commandContext->set_graphics_pipeline(m_pipelineHandle);
            commandContext->set_primitive_topology(
                RHI::PrimitiveTopologyType::Line);
            commandContext->set_cbv(0, m_viewProjectionBufferHandle);
            commandContext->set_cbv(1, m_selectionBufferHandle);
            commandContext->draw_instanced(
                k_vertexCount,
                GpuData::k_maxDebugSelectionItemCount,
                0,
                0);
        }

    private:
        static constexpr uint32_t k_vertexCount = 30;

        RHI::BufferHandle m_viewProjectionBufferHandle{};
        RHI::BufferHandle m_selectionBufferHandle{};
        RHI::TextureHandle m_colorHandle{};
        RHI::ViewHandle m_colorRtvHandle{};
        RHI::RootSignatureHandle m_rootSignatureHandle{};
        RHI::ShaderBlobHandle m_vertexShaderHandle{};
        RHI::ShaderBlobHandle m_pixelShaderHandle{};
        RHI::PipelineStateHandle m_pipelineHandle{};
    };
} // namespace Cue::DrawSystem
