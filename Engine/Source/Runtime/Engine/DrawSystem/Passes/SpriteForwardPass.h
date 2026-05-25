#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <DrawSystem/DrawFrameState.h>

// === C++ includes ===
#include <string>
#include <utility>

namespace Cue::DrawSystem
{
    class SpriteForwardPass final : public RHI::FrameGraphPass
    {
    public:
        SpriteForwardPass(std::string a_name,
            std::string a_colorName,
            std::string a_colorRtvName,
            const DrawFrameState& a_drawFrameState,
            RHI::BufferHandle a_spriteInstanceBufferHandle)
            : m_name(std::move(a_name))
            , m_colorName(std::move(a_colorName))
            , m_colorRtvName(std::move(a_colorRtvName))
            , m_drawFrameState(a_drawFrameState)
            , m_spriteInstanceBufferHandle(a_spriteInstanceBufferHandle)
        {}

        const char* name() const noexcept override { return m_name.c_str(); }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Graphics;
        }

        bool is_enabled(uint32_t a_frameIndex) const noexcept override
        {
            if (a_frameIndex >= m_drawFrameState.frameStates.size())
            {
                return false;
            }

            return m_drawFrameState.frame_state(a_frameIndex).spriteCount > 0;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.get_texture(m_colorName, m_colorHandle);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to get final color texture for SpriteForward pass.");
            }

            result = builder.get_view(m_colorRtvName, m_colorRtvHandle);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to get final color RTV for SpriteForward pass.");
            }

            result = builder.read_buffer(m_spriteInstanceBufferHandle);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "SpriteForwardRootSignature";
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::DescriptorTableSRV,
                RHI::ShaderVisibility::Pixel,
                0,
                0,
                1 });
            result = builder.create_root_signature(
                rootSignatureDesc, m_rootSignatureHandle);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to create root signature for SpriteForward pass.");
            }

            RHI::ShaderCompileDesc vertexShaderDesc{};
            vertexShaderDesc.name = "SpriteForwardVS";
            vertexShaderDesc.filePath = "Shaders/D3D12/SpriteForward.hlsl";
            vertexShaderDesc.entryPoint = "vs_main";
            vertexShaderDesc.targetProfile = "vs_6_0";
            result = builder.create_shader_blob(vertexShaderDesc, m_vertexShaderHandle);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to compile SpriteForward vertex shader.");
            }

            RHI::ShaderCompileDesc pixelShaderDesc{};
            pixelShaderDesc.name = "SpriteForwardPS";
            pixelShaderDesc.filePath = "Shaders/D3D12/SpriteForward.hlsl";
            pixelShaderDesc.entryPoint = "ps_main";
            pixelShaderDesc.targetProfile = "ps_6_0";
            result = builder.create_shader_blob(pixelShaderDesc, m_pixelShaderHandle);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to compile SpriteForward pixel shader.");
            }

            RHI::GraphicsPipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "SpriteForwardPipeline";
            pipelineDesc.rootSignatureHandle = m_rootSignatureHandle;
            pipelineDesc.vsHandle = m_vertexShaderHandle;
            pipelineDesc.psHandle = m_pixelShaderHandle;
            pipelineDesc.rasterizerState.cullMode = RHI::CullMode::None;
            pipelineDesc.depthStencilState.depthEnable = false;
            pipelineDesc.depthStencilState.depthWriteMask = RHI::DepthWriteMask::Zero;
            pipelineDesc.blendMode = { RHI::BlendMode::Normal };
            pipelineDesc.rtvFormats = { RHI::ColorFormat::R8G8B8A8_UNORM };
            result = builder.create_graphics_pipeline(pipelineDesc, m_pipelineHandle);
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

            return builder.use_buffer(
                m_spriteInstanceBufferHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::Common);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            const DrawFrameData& frameState =
                m_drawFrameState.frame_state(context.frame_index());
            if (!m_spriteInstanceBufferHandle.valid() ||
                frameState.spriteCount == 0)
            {
                return;
            }

            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            commandContext->set_render_targets(&m_colorRtvHandle, 1, {});
            commandContext->set_viewport_scissor(context.width(), context.height());
            commandContext->set_graphics_pipeline(m_pipelineHandle);
            commandContext->set_primitive_topology(
                RHI::PrimitiveTopologyType::Triangle);
            commandContext->set_srv(0, m_spriteInstanceBufferHandle);
            commandContext->set_graphics_texture_table(1);
            commandContext->draw_instanced(6, frameState.spriteCount, 0, 0);
        }

    private:
        std::string m_name{};
        std::string m_colorName{};
        std::string m_colorRtvName{};
        const DrawFrameState& m_drawFrameState;
        RHI::BufferHandle m_spriteInstanceBufferHandle{};
        RHI::TextureHandle m_colorHandle{};
        RHI::ViewHandle m_colorRtvHandle{};
        RHI::RootSignatureHandle m_rootSignatureHandle{};
        RHI::ShaderBlobHandle m_vertexShaderHandle{};
        RHI::ShaderBlobHandle m_pixelShaderHandle{};
        RHI::PipelineStateHandle m_pipelineHandle{};
    };
} // namespace Cue::DrawSystem
