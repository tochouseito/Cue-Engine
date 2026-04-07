#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <GameCore/RenderSceneState.h>

// === C++ includes ===
#include <array>

namespace Cue
{
    class StaticMeshForwardPass final : public RHI::FrameGraphPass
    {
    public:
        StaticMeshForwardPass(const RenderFrameState& a_frameState,
            uint32_t a_indexCountPerInstance)
            : m_frameState(a_frameState),
            m_indexCountPerInstance(a_indexCountPerInstance)
        {}

        const char* name() const noexcept override { return "StaticMeshForward"; }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Graphics;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.get_texture("FinalColor", m_finalColorHandle);
            if (!result)
            {
                return result;
            }
            result = builder.render(&m_finalColorHandle, 1);
            if (!result)
            {
                return result;
            }
            result = builder.get_view("FinalColorRTV", m_finalColorRtvHandle);
            if (!result)
            {
                return result;
            }

            result =
                builder.get_buffer("RenderObjectBuffer", m_renderObjectBufferHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("TransformBuffer", m_transformBufferHandle);
            if (!result)
            {
                return result;
            }
            result =
                builder.get_buffer("StaticMeshPool.Position", m_positionBufferHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("StaticMeshPool.Uv", m_uvBufferHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("StaticMeshPool.Normal", m_normalBufferHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("StaticMeshPool.Index", m_indexBufferHandle);
            if (!result)
            {
                return result;
            }
            result =
                builder.get_buffer("StaticMeshPool.MeshRange", m_meshRangeBufferHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("VisibleObjectCountBuffer",
                m_visibleObjectCountBufferHandle);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "StaticMeshForwardRootSignature";
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::Vertex, 0 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::Vertex, 1 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::Vertex, 2 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::Vertex, 3 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::Vertex, 4 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::Vertex, 5 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::Vertex, 6 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::Vertex, 7 });
            result =
                builder.create_root_signature(rootSignatureDesc, m_rootSignatureHandle);
            if (!result)
            {
                return Result::fail(
                    result.code, Severity::Error,
                    "Failed to create root signature for StaticMeshForward pass.");
            }

            RHI::ShaderCompileDesc vertexShaderDesc{};
            vertexShaderDesc.name = "StaticMeshForwardVS";
            vertexShaderDesc.filePath = "Shaders/D3D12/StaticMeshForward.hlsl";
            vertexShaderDesc.entryPoint = "vs_main";
            vertexShaderDesc.targetProfile = "vs_6_0";
            result = builder.create_shader_blob(vertexShaderDesc, m_vertexShaderHandle);
            if (!result)
            {
                return Result::fail(result.code, Severity::Error,
                    "Failed to compile StaticMeshForward vertex shader.");
            }

            RHI::ShaderCompileDesc pixelShaderDesc{};
            pixelShaderDesc.name = "StaticMeshForwardPS";
            pixelShaderDesc.filePath = "Shaders/D3D12/StaticMeshForward.hlsl";
            pixelShaderDesc.entryPoint = "ps_main";
            pixelShaderDesc.targetProfile = "ps_6_0";
            result = builder.create_shader_blob(pixelShaderDesc, m_pixelShaderHandle);
            if (!result)
            {
                return Result::fail(result.code, Severity::Error,
                    "Failed to compile StaticMeshForward pixel shader.");
            }

            RHI::GraphicsPipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "StaticMeshForwardPipeline";
            pipelineDesc.rootSignatureHandle = m_rootSignatureHandle;
            pipelineDesc.vsHandle = m_vertexShaderHandle;
            pipelineDesc.psHandle = m_pixelShaderHandle;
            pipelineDesc.rasterizerState.cullMode = RHI::CullMode::Back;
            pipelineDesc.depthStencilState.depthEnable = false;
            pipelineDesc.depthStencilState.depthWriteMask = RHI::DepthWriteMask::Zero;
            pipelineDesc.blendMode = { RHI::BlendMode::None };
            pipelineDesc.rtvFormats = { RHI::ColorFormat::R8G8B8A8_UNORM };
            result = builder.create_graphics_pipeline(pipelineDesc, m_pipelineHandle);
            if (!result)
            {
                return Result::fail(
                    result.code, Severity::Error,
                    "Failed to create graphics pipeline for StaticMeshForward pass.");
            }

            return Result::ok();
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            {
                RHI::ResourceBarrierDesc barrierDesc{};
                barrierDesc.after = RHI::ResourceState::RenderTarget;
                commandContext->resource_barrier(m_finalColorHandle, barrierDesc);
            }
            {
                RHI::ResourceBarrierDesc barrierDesc{};
                barrierDesc.after = RHI::ResourceState::ShaderResource;
                commandContext->resource_barrier(m_renderObjectBufferHandle, barrierDesc);
                commandContext->resource_barrier(m_transformBufferHandle, barrierDesc);
                commandContext->resource_barrier(m_positionBufferHandle, barrierDesc);
                commandContext->resource_barrier(m_uvBufferHandle, barrierDesc);
                commandContext->resource_barrier(m_normalBufferHandle, barrierDesc);
                commandContext->resource_barrier(m_indexBufferHandle, barrierDesc);
                commandContext->resource_barrier(m_meshRangeBufferHandle, barrierDesc);
                commandContext->resource_barrier(m_visibleObjectCountBufferHandle,
                    barrierDesc);
            }

            commandContext->clear_render_target(m_finalColorRtvHandle,
                k_clearColor.data());
            commandContext->set_render_targets(&m_finalColorRtvHandle, 1, {});
            commandContext->set_viewport_scissor(context.width(), context.height());
            commandContext->set_graphics_pipeline(m_pipelineHandle);
            commandContext->set_primitive_topology(
                RHI::PrimitiveTopologyType::Triangle);
            commandContext->set_srv(0, m_renderObjectBufferHandle);
            commandContext->set_srv(1, m_transformBufferHandle);
            commandContext->set_srv(2, m_positionBufferHandle);
            commandContext->set_srv(3, m_uvBufferHandle);
            commandContext->set_srv(4, m_normalBufferHandle);
            commandContext->set_srv(5, m_indexBufferHandle);
            commandContext->set_srv(6, m_meshRangeBufferHandle);
            commandContext->set_srv(7, m_visibleObjectCountBufferHandle);

            if (m_indexCountPerInstance > 0 && m_frameState.objectCount > 0)
            {
                commandContext->draw_instanced(m_indexCountPerInstance,
                    m_frameState.objectCount, 0, 0);
            }

            {
                RHI::ResourceBarrierDesc barrierDesc{};
                barrierDesc.after = RHI::ResourceState::ShaderResource;
                commandContext->resource_barrier(m_finalColorHandle, barrierDesc);
            }
            {
                RHI::ResourceBarrierDesc barrierDesc{};
                barrierDesc.after = RHI::ResourceState::Common;
                commandContext->resource_barrier(m_renderObjectBufferHandle, barrierDesc);
                commandContext->resource_barrier(m_transformBufferHandle, barrierDesc);
                commandContext->resource_barrier(m_visibleObjectCountBufferHandle,
                    barrierDesc);
            }
        }

    private:
        static constexpr std::array<float, 4> k_clearColor = { 0.0f, 0.5f, 0.0f, 1.0f };

        const RenderFrameState& m_frameState;
        uint32_t m_indexCountPerInstance = 0;
        RHI::textureHandle m_finalColorHandle{};
        RHI::viewHandle m_finalColorRtvHandle{};
        RHI::bufferHandle m_renderObjectBufferHandle{};
        RHI::bufferHandle m_transformBufferHandle{};
        RHI::bufferHandle m_positionBufferHandle{};
        RHI::bufferHandle m_uvBufferHandle{};
        RHI::bufferHandle m_normalBufferHandle{};
        RHI::bufferHandle m_indexBufferHandle{};
        RHI::bufferHandle m_meshRangeBufferHandle{};
        RHI::bufferHandle m_visibleObjectCountBufferHandle{};
        RHI::rootSignatureHandle m_rootSignatureHandle{};
        RHI::shaderBlobHandle m_vertexShaderHandle{};
        RHI::shaderBlobHandle m_pixelShaderHandle{};
        RHI::pipelineStateHandle m_pipelineHandle{};
    };
} // namespace Cue
