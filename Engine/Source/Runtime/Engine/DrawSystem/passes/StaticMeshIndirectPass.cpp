#include "StaticMeshIndirectPass.h"

// === C++ includes ===
#include <utility>

namespace Cue::DrawSystem
{
    StaticMeshIndirectPass::StaticMeshIndirectPass(
        DrawResources& a_drawResources,
        MeshPool& a_meshPool,
        DrawFrameState& a_drawFrameState)
        : StaticMeshIndirectPass(
            a_drawResources,
            a_meshPool,
            a_drawFrameState,
            "StaticMeshIndirect",
            "FinalColor")
    {
    }

    StaticMeshIndirectPass::StaticMeshIndirectPass(
        DrawResources& a_drawResources,
        MeshPool& a_meshPool,
        DrawFrameState& a_drawFrameState,
        std::string a_passName,
        std::string a_renderTargetName)
        : m_drawResources(a_drawResources)
        , m_meshPool(a_meshPool)
        , m_drawFrameState(a_drawFrameState)
        , m_passName(std::move(a_passName))
        , m_renderTargetName(std::move(a_renderTargetName))
        , m_renderTargetRtvName(m_renderTargetName + "RTV")
    {
    }

    StaticMeshIndirectPass::~StaticMeshIndirectPass() = default;

    const char* StaticMeshIndirectPass::name() const noexcept
    {
        return m_passName.c_str();
    }

    RHI::CommandListType StaticMeshIndirectPass::type() const noexcept
    {
        return RHI::CommandListType::Graphics;
    }

    Result StaticMeshIndirectPass::setup(RHI::FrameGraphBuilder& a_builder)
    {
        Result result = a_builder.get_texture(m_renderTargetName, m_renderTargetHandle);
        if (!result)
        {
            return Result::fail(
                result.code,
                Severity::Error,
                "Failed to get color texture handle for static mesh indirect pass.");
        }

        result = a_builder.render(&m_renderTargetHandle, 1);
        if (!result)
        {
            return Result::fail(
                result.code,
                Severity::Error,
                "Failed to declare color texture as render target for static mesh indirect pass.");
        }

        result = a_builder.get_view(m_renderTargetRtvName, m_renderTargetRtvHandle);
        if (!result)
        {
            return Result::fail(
                result.code,
                Severity::Error,
                "Failed to get color RTV view handle for static mesh indirect pass.");
        }

        result = m_meshPool.get_bindings(m_meshPoolBindings);
        if (!result)
        {
            return result;
        }

        RHI::RootSignatureDesc rootSignatureDesc{};
        rootSignatureDesc.name = m_passName + "RootSignature";
        rootSignatureDesc.parameters.push_back(
            RHI::RootParameterDesc
            {
                RHI::RootParameterType::_32BitConstants,
                RHI::ShaderVisibility::Vertex,
                0
            });
        rootSignatureDesc.parameters.push_back(
            RHI::RootParameterDesc
            {
                RHI::RootParameterType::SRV,
                RHI::ShaderVisibility::Vertex,
                0
            });
        rootSignatureDesc.parameters.push_back(
            RHI::RootParameterDesc
            {
                RHI::RootParameterType::CBV,
                RHI::ShaderVisibility::Vertex,
                1
            });
        rootSignatureDesc.parameters.push_back(
            RHI::RootParameterDesc
            {
                RHI::RootParameterType::SRV,
                RHI::ShaderVisibility::Vertex,
                1
            });

        result = a_builder.create_root_signature(rootSignatureDesc, m_rootSignature);
        if (!result)
        {
            return Result::fail(
                result.code,
                Severity::Error,
                "Failed to create root signature for static mesh indirect pass.");
        }

        const std::string shaderFilePath = "Shaders/D3D12/StaticMeshIndirectForward.hlsl";

        RHI::ShaderCompileDesc vertexShaderDesc{};
        vertexShaderDesc.name = m_passName + "VS";
        vertexShaderDesc.filePath = shaderFilePath;
        vertexShaderDesc.entryPoint = "vs_main";
        vertexShaderDesc.targetProfile = "vs_6_0";
        result = a_builder.create_shader_blob(vertexShaderDesc, m_vertexShader);
        if (!result)
        {
            return Result::fail(
                result.code,
                Severity::Error,
                "Failed to compile static mesh indirect vertex shader.");
        }

        RHI::ShaderCompileDesc pixelShaderDesc{};
        pixelShaderDesc.name = m_passName + "PS";
        pixelShaderDesc.filePath = shaderFilePath;
        pixelShaderDesc.entryPoint = "ps_main";
        pixelShaderDesc.targetProfile = "ps_6_0";
        result = a_builder.create_shader_blob(pixelShaderDesc, m_pixelShader);
        if (!result)
        {
            return Result::fail(
                result.code,
                Severity::Error,
                "Failed to compile static mesh indirect pixel shader.");
        }

        RHI::GraphicsPipelineStateDesc pipelineDesc{};
        pipelineDesc.name = m_passName + "Pipeline";
        pipelineDesc.rootSignatureHandle = m_rootSignature;
        pipelineDesc.vsHandle = m_vertexShader;
        pipelineDesc.psHandle = m_pixelShader;
        pipelineDesc.inputElements.push_back(
            RHI::InputElementDesc
            {
                "POSITION",
                0,
                RHI::InputElementFormat::R32G32B32A32_Float,
                0,
                0
            });
        pipelineDesc.rasterizerState.cullMode = RHI::CullMode::None;
        pipelineDesc.depthStencilState.depthEnable = false;
        pipelineDesc.depthStencilState.depthWriteMask = RHI::DepthWriteMask::Zero;
        pipelineDesc.blendMode = { RHI::BlendMode::None };
        pipelineDesc.rtvFormats = { RHI::ColorFormat::R8G8B8A8_UNORM };
        result = a_builder.create_graphics_pipeline(pipelineDesc, m_pipelineState);
        if (!result)
        {
            return Result::fail(
                result.code,
                Severity::Error,
                "Failed to create pipeline for static mesh indirect pass.");
        }

        return Result::ok();
    }

    Result StaticMeshIndirectPass::describe_resources(RHI::FrameGraphBuilder& a_builder)
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

        result = a_builder.use_buffer(
            m_meshPoolBindings.positionBuffer,
            RHI::ResourceAccessType::Read,
            RHI::ResourceState::VertexBuffer,
            RHI::ResourceState::VertexBuffer);
        if (!result)
        {
            return result;
        }

        result = a_builder.use_buffer(
            m_meshPoolBindings.uvBuffer,
            RHI::ResourceAccessType::Read,
            RHI::ResourceState::VertexBuffer,
            RHI::ResourceState::VertexBuffer);
        if (!result)
        {
            return result;
        }

        result = a_builder.use_buffer(
            m_meshPoolBindings.normalBuffer,
            RHI::ResourceAccessType::Read,
            RHI::ResourceState::VertexBuffer,
            RHI::ResourceState::VertexBuffer);
        if (!result)
        {
            return result;
        }

        result = a_builder.use_buffer(
            m_meshPoolBindings.indexBuffer,
            RHI::ResourceAccessType::Read,
            RHI::ResourceState::IndexBuffer,
            RHI::ResourceState::IndexBuffer);
        if (!result)
        {
            return result;
        }

        result = a_builder.use_buffer(
            m_drawResources.view_projection_buffer_handle(),
            RHI::ResourceAccessType::Read,
            RHI::ResourceState::VertexBuffer,
            RHI::ResourceState::VertexBuffer);
        if (!result)
        {
            return result;
        }

        return a_builder.use_buffer(
            m_drawResources.transform_buffer_handle(),
            RHI::ResourceAccessType::Read,
            RHI::ResourceState::ShaderResource,
            RHI::ResourceState::ShaderResource);
    }

    void StaticMeshIndirectPass::execute(RHI::FrameGraphContext& a_context)
    {
        RHI::ICommandContext* commandContext = a_context.commandContext();
        if (commandContext == nullptr)
        {
            return;
        }

        const DrawFrameData& frameData = m_drawFrameState.frame_state(a_context.frame_index());
        // ImGui が SRV として読む color target は無描画 frame でも alpha を含めて定義済みにする
        commandContext->clear_render_target(m_renderTargetRtvHandle, k_clearColor.data());
        commandContext->set_render_targets(&m_renderTargetRtvHandle, 1, {});
        commandContext->set_viewport_scissor(a_context.width(), a_context.height());
        if (frameData.indirectCommandCount == 0)
        {
            return;
        }

        commandContext->set_graphics_pipeline(m_pipelineState);
        commandContext->set_primitive_topology(RHI::PrimitiveTopologyType::Triangle);
        commandContext->set_vertex_buffer(0, m_meshPoolBindings.positionBuffer);
        commandContext->set_vertex_buffer(1, m_meshPoolBindings.uvBuffer);
        commandContext->set_vertex_buffer(2, m_meshPoolBindings.normalBuffer);
        commandContext->set_index_buffer(m_meshPoolBindings.indexBuffer, RHI::IndexFormat::UInt32);
        commandContext->set_srv(1, m_drawResources.static_mesh_object_index_buffer_handle());
        commandContext->set_cbv(2, m_drawResources.view_projection_buffer_handle());
        commandContext->set_srv(3, m_drawResources.transform_buffer_handle());
        commandContext->execute_indexed_indirect(
            m_drawResources.static_mesh_indirect_command_buffer_handle(),
            m_drawResources.static_mesh_indirect_command_count_buffer_handle(),
            frameData.indirectCommandCount);
    }
} // namespace Cue::DrawSystem
