#include "GridPass.h"

// === C++ includes ===
#include <utility>

namespace Cue::DrawSystem
{
    GridPass::GridPass(DrawResources& a_drawResources)
        : GridPass(a_drawResources, "Grid", "FinalColor")
    {
    }

    GridPass::GridPass(DrawResources& a_drawResources, std::string a_passName, std::string a_renderTargetName)
        : m_drawResources(a_drawResources)
        , m_passName(std::move(a_passName))
        , m_renderTargetName(std::move(a_renderTargetName))
        , m_renderTargetRtvName(m_renderTargetName + "RTV")
    {
    }

    GridPass::~GridPass() = default;

    const char* GridPass::name() const noexcept
    {
        return m_passName.c_str();
    }

    RHI::CommandListType GridPass::type() const noexcept
    {
        return RHI::CommandListType::Graphics;
    }

    Result GridPass::setup(RHI::FrameGraphBuilder& a_builder)
    {
        Result result = a_builder.get_texture(m_renderTargetName, m_renderTargetHandle);
        if (!result)
        {
            return Result::fail(result.code, Severity::Error, "Failed to get color texture handle for grid pass.");
        }

        result = a_builder.render(&m_renderTargetHandle, 1);
        if (!result)
        {
            return Result::fail(result.code, Severity::Error, "Failed to declare color texture for grid pass.");
        }

        result = a_builder.get_view(m_renderTargetRtvName, m_renderTargetRtvHandle);
        if (!result)
        {
            return Result::fail(result.code, Severity::Error, "Failed to get color RTV view handle for grid pass.");
        }

        RHI::RootSignatureDesc rootSignatureDesc{};
        rootSignatureDesc.name = m_passName + "RootSignature";
        rootSignatureDesc.parameters.push_back(
            RHI::RootParameterDesc{RHI::RootParameterType::CBV, RHI::ShaderVisibility::Vertex, 0});

        result = a_builder.create_root_signature(rootSignatureDesc, m_rootSignature);
        if (!result)
        {
            return Result::fail(result.code, Severity::Error, "Failed to create root signature for grid pass.");
        }

        const std::string shaderFilePath = "Shaders/D3D12/Grid.hlsl";

        RHI::ShaderCompileDesc vertexShaderDesc{};
        vertexShaderDesc.name = m_passName + "VS";
        vertexShaderDesc.filePath = shaderFilePath;
        vertexShaderDesc.entryPoint = "vs_main";
        vertexShaderDesc.targetProfile = "vs_6_0";
        result = a_builder.create_shader_blob(vertexShaderDesc, m_vertexShader);
        if (!result)
        {
            return Result::fail(result.code, Severity::Error, "Failed to compile grid vertex shader.");
        }

        RHI::ShaderCompileDesc pixelShaderDesc{};
        pixelShaderDesc.name = m_passName + "PS";
        pixelShaderDesc.filePath = shaderFilePath;
        pixelShaderDesc.entryPoint = "ps_main";
        pixelShaderDesc.targetProfile = "ps_6_0";
        result = a_builder.create_shader_blob(pixelShaderDesc, m_pixelShader);
        if (!result)
        {
            return Result::fail(result.code, Severity::Error, "Failed to compile grid pixel shader.");
        }

        RHI::GraphicsPipelineStateDesc pipelineDesc{};
        pipelineDesc.name = m_passName + "Pipeline";
        pipelineDesc.rootSignatureHandle = m_rootSignature;
        pipelineDesc.vsHandle = m_vertexShader;
        pipelineDesc.psHandle = m_pixelShader;
        pipelineDesc.rasterizerState.cullMode = RHI::CullMode::None;
        pipelineDesc.depthStencilState.depthEnable = false;
        pipelineDesc.depthStencilState.depthWriteMask = RHI::DepthWriteMask::Zero;
        pipelineDesc.blendMode = {RHI::BlendMode::Normal};
        pipelineDesc.primitiveTopologyType = RHI::PrimitiveTopologyType::Line;
        pipelineDesc.rtvFormats = {RHI::ColorFormat::R8G8B8A8_UNORM};
        result = a_builder.create_graphics_pipeline(pipelineDesc, m_pipelineState);
        if (!result)
        {
            return Result::fail(result.code, Severity::Error, "Failed to create pipeline for grid pass.");
        }

        return Result::ok();
    }

    Result GridPass::describe_resources(RHI::FrameGraphBuilder& a_builder)
    {
        Result result = a_builder.use_texture(
            m_renderTargetHandle,
            RHI::ResourceAccessType::Write,
            RHI::ResourceState::RenderTarget,
            RHI::ResourceState::RenderTarget);
        if (!result)
        {
            return result;
        }

        return a_builder.use_buffer(
            m_drawResources.view_projection_buffer_handle(),
            RHI::ResourceAccessType::Read,
            RHI::ResourceState::VertexBuffer,
            RHI::ResourceState::VertexBuffer);
    }

    void GridPass::execute(RHI::FrameGraphContext& a_context)
    {
        RHI::ICommandContext* commandContext = a_context.commandContext();
        if (commandContext == nullptr)
        {
            return;
        }

        commandContext->set_render_targets(&m_renderTargetRtvHandle, 1, {});
        commandContext->set_viewport_scissor(a_context.width(), a_context.height());
        commandContext->set_graphics_pipeline(m_pipelineState);
        commandContext->set_primitive_topology(RHI::PrimitiveTopologyType::Line);
        commandContext->set_cbv(0, m_drawResources.view_projection_buffer_handle());
        commandContext->draw_instanced(2, k_gridLineCount, 0, 0);
    }
} // namespace Cue::DrawSystem
