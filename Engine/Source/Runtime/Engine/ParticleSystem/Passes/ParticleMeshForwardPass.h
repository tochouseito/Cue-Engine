// ParticleMeshForwardPass の役割と公開要素を定義する

#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <ParticleSystem/ParticleFrameState.h>

// === C++ includes ===
#include <string>
#include <utility>

namespace Cue::ParticleSystem
{
    class ParticleMeshForwardPass final : public RHI::FrameGraphPass
    {
    public:
        ParticleMeshForwardPass(std::string a_name,
            std::string a_colorName,
            std::string a_colorRtvName,
            std::string a_depthName,
            std::string a_depthDsvName,
            const ParticleFrameState& a_frameState,
            RHI::BufferHandle a_viewProjectionBufferHandle,
            RHI::BufferHandle a_particleBufferHandle)
            : m_name(std::move(a_name))
            , m_colorName(std::move(a_colorName))
            , m_colorRtvName(std::move(a_colorRtvName))
            , m_depthName(std::move(a_depthName))
            , m_depthDsvName(std::move(a_depthDsvName))
            , m_frameState(a_frameState)
            , m_viewProjectionBufferHandle(a_viewProjectionBufferHandle)
            , m_particleBufferHandle(a_particleBufferHandle)
        {}

        const char* name() const noexcept override { return m_name.c_str(); }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Graphics;
        }

        bool is_enabled(uint32_t a_frameIndex) const noexcept override
        {
            return a_frameIndex < m_frameState.frameStates.size() &&
                m_frameState.frame_state(a_frameIndex).frame.particleCount > 0;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.get_texture(m_colorName, m_colorHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_view(m_colorRtvName, m_colorRtvHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_texture(m_depthName, m_depthHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_view(m_depthDsvName, m_depthDsvHandle);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_viewProjectionBufferHandle);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_particleBufferHandle);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = m_name + "RootSignature";
            rootSignatureDesc.parameters.push_back({
                RHI::RootParameterType::CBV, RHI::ShaderVisibility::Vertex, 0 });
            rootSignatureDesc.parameters.push_back({
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back({
                RHI::RootParameterType::DescriptorTableSRV,
                RHI::ShaderVisibility::Pixel,
                0,
                0,
                1 });
            result = builder.create_root_signature(
                rootSignatureDesc, m_rootSignatureHandle);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc vertexShaderDesc{};
            vertexShaderDesc.name = m_name + "VS";
            vertexShaderDesc.filePath = "Shaders/D3D12/ParticleMeshForward.hlsl";
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
            pixelShaderDesc.filePath = "Shaders/D3D12/ParticleMeshForward.hlsl";
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
            pipelineDesc.depthStencilState.depthWriteMask = RHI::DepthWriteMask::Zero;
            pipelineDesc.depthStencilState.depthFunc = RHI::ComparisonFunc::LessEqual;
            pipelineDesc.blendMode = { RHI::BlendMode::Normal };
            pipelineDesc.dsvFormat = RHI::ColorFormat::D24_UNorm_S8_UInt;
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
            result = builder.use_texture(
                m_depthHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::DepthWrite,
                RHI::ResourceState::Common);
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
                m_particleBufferHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            const ParticleFrameData& frameState =
                m_frameState.frame_state(context.frame_index());
            if (frameState.frame.particleCount == 0)
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
                RHI::PrimitiveTopologyType::Triangle);
            commandContext->set_cbv(0, m_viewProjectionBufferHandle);
            commandContext->set_srv(1, m_particleBufferHandle);
            commandContext->set_graphics_texture_table(2);
            commandContext->draw_instanced(
                12,
                frameState.frame.particleCount,
                0,
                0);
        }

    private:
        std::string m_name{};
        std::string m_colorName{};
        std::string m_colorRtvName{};
        std::string m_depthName{};
        std::string m_depthDsvName{};
        const ParticleFrameState& m_frameState;
        RHI::BufferHandle m_viewProjectionBufferHandle{};
        RHI::BufferHandle m_particleBufferHandle{};
        RHI::TextureHandle m_colorHandle{};
        RHI::ViewHandle m_colorRtvHandle{};
        RHI::TextureHandle m_depthHandle{};
        RHI::ViewHandle m_depthDsvHandle{};
        RHI::RootSignatureHandle m_rootSignatureHandle{};
        RHI::ShaderBlobHandle m_vertexShaderHandle{};
        RHI::ShaderBlobHandle m_pixelShaderHandle{};
        RHI::PipelineStateHandle m_pipelineHandle{};
    };
} // namespace Cue::ParticleSystem
