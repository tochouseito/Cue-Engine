#pragma once

/// ************************************************************************************
/// Mesh forward pass
/// ************************************************************************************

// === DrawSystem includes ===
#include "DrawSystem/MeshPool.h"

// === C++ includes ===
#include <limits>

namespace Cue::DrawSystem
{
    class MeshForwardPass final : public RHI::FrameGraphPass
    {
    public:
        static constexpr uint32_t k_invalidMeshId =
            (std::numeric_limits<uint32_t>::max)();

        MeshForwardPass(MeshPool& meshPool, const uint32_t& meshId)
            : m_meshPool(meshPool)
            , m_meshId(meshId)
        {}

        const char* name() const noexcept override
        {
            return "MeshForward";
        }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Graphics;
        }

        bool is_enabled(uint32_t a_frameIndex) const noexcept override
        {
            a_frameIndex;
            if (m_meshId == k_invalidMeshId)
            {
                return false;
            }

            MeshRange range{};
            return m_meshPool.get_mesh_range(m_meshId, range) && range.indexCount > 0;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.get_texture("FinalColor", m_finalColorHandle);
            if (!result)
            {
                return Result::fail(
                    Code::GetFailed,
                    Severity::Error,
                    "Failed to get FinalColor texture handle for mesh forward pass.");
            }

            result = builder.get_view("FinalColorRTV", m_finalColorRtvHandle);
            if (!result)
            {
                return Result::fail(
                    Code::GetFailed,
                    Severity::Error,
                    "Failed to get FinalColor RTV handle for mesh forward pass.");
            }

            result = m_meshPool.get_bindings(m_meshBindings);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "MeshForwardRootSignature";
            result = builder.create_root_signature(
                rootSignatureDesc, m_rootSignatureHandle);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to create root signature for mesh forward pass.");
            }

            const std::string shaderFilePath = "Shaders/D3D12/MeshForward.hlsl";

            RHI::ShaderCompileDesc vertexShaderDesc{};
            vertexShaderDesc.name = "MeshForwardVS";
            vertexShaderDesc.filePath = shaderFilePath;
            vertexShaderDesc.entryPoint = "vs_main";
            vertexShaderDesc.targetProfile = "vs_6_0";
            result = builder.create_shader_blob(vertexShaderDesc, m_vsHandle);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to compile mesh forward vertex shader.");
            }

            RHI::ShaderCompileDesc pixelShaderDesc{};
            pixelShaderDesc.name = "MeshForwardPS";
            pixelShaderDesc.filePath = shaderFilePath;
            pixelShaderDesc.entryPoint = "ps_main";
            pixelShaderDesc.targetProfile = "ps_6_0";
            result = builder.create_shader_blob(pixelShaderDesc, m_psHandle);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to compile mesh forward pixel shader.");
            }

            RHI::GraphicsPipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "MeshForwardPipeline";
            pipelineDesc.rootSignatureHandle = m_rootSignatureHandle;
            pipelineDesc.vsHandle = m_vsHandle;
            pipelineDesc.psHandle = m_psHandle;
            pipelineDesc.inputElements.push_back(
                RHI::InputElementDesc{
                    "POSITION",
                    0,
                    RHI::InputElementFormat::R32G32B32A32_Float,
                    0,
                    0 });
            pipelineDesc.rasterizerState.cullMode = RHI::CullMode::None;
            pipelineDesc.depthStencilState.depthEnable = false;
            pipelineDesc.depthStencilState.depthWriteMask = RHI::DepthWriteMask::Zero;
            pipelineDesc.blendMode = { RHI::BlendMode::None };
            pipelineDesc.rtvFormats = { RHI::ColorFormat::R8G8B8A8_UNORM };
            result = builder.create_graphics_pipeline(pipelineDesc, m_pipelineHandle);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to create graphics pipeline for mesh forward pass.");
            }

            return Result::ok();
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.use_texture(
                m_finalColorHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::RenderTarget,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }

            result = builder.use_buffer(
                m_meshBindings.positionBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::VertexBuffer,
                RHI::ResourceState::VertexBuffer);
            if (!result)
            {
                return result;
            }

            return builder.use_buffer(
                m_meshBindings.indexBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::IndexBuffer,
                RHI::ResourceState::IndexBuffer);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            MeshRange range{};
            Result result = m_meshPool.get_mesh_range(m_meshId, range);
            if (!result || range.indexCount == 0)
            {
                return;
            }

            RHI::ICommandContext* commandContext = context.commandContext();
            commandContext->set_render_targets(&m_finalColorRtvHandle, 1, {});
            commandContext->set_viewport_scissor(context.width(), context.height());
            commandContext->set_graphics_pipeline(m_pipelineHandle);
            commandContext->set_primitive_topology(RHI::PrimitiveTopologyType::Triangle);
            commandContext->set_vertex_buffer(0, m_meshBindings.positionBuffer);
            commandContext->set_index_buffer(m_meshBindings.indexBuffer, RHI::IndexFormat::UInt32);
            commandContext->draw_indexed_instanced(
                range.indexCount,
                1,
                range.startIndex,
                range.baseVertex,
                0);
        }

    private:
        MeshPool& m_meshPool;
        const uint32_t& m_meshId;
        MeshPoolBindings m_meshBindings{};
        RHI::TextureHandle m_finalColorHandle{};
        RHI::ViewHandle m_finalColorRtvHandle{};
        RHI::RootSignatureHandle m_rootSignatureHandle{};
        RHI::ShaderBlobHandle m_vsHandle{};
        RHI::ShaderBlobHandle m_psHandle{};
        RHI::PipelineStateHandle m_pipelineHandle{};
    };
}
