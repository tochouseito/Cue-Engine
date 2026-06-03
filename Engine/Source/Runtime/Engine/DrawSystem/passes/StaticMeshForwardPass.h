#pragma once

/// ****************************************************************************
/// Static mesh forward pass
/// ****************************************************************************

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include "DrawSystem/DrawFrameState.h"

namespace Cue::DrawSystem
{
    class StaticMeshForwardPass final : public RHI::FrameGraphPass
    {
    public:
        StaticMeshForwardPass(
            const DrawFrameState& drawFrameState,
            RHI::BufferHandle renderObjectBuffer,
            RHI::BufferHandle transformBuffer,
            RHI::BufferHandle viewProjectionBuffer,
            RHI::BufferHandle visibleObjectCountBuffer,
            RHI::BufferHandle materialBuffer,
            RHI::BufferHandle lightFrameBuffer,
            RHI::BufferHandle pointLightBuffer)
            : m_drawFrameState(drawFrameState)
            , m_renderObjectBuffer(renderObjectBuffer)
            , m_transformBuffer(transformBuffer)
            , m_viewProjectionBuffer(viewProjectionBuffer)
            , m_visibleObjectCountBuffer(visibleObjectCountBuffer)
            , m_materialBuffer(materialBuffer)
            , m_lightFrameBuffer(lightFrameBuffer)
            , m_pointLightBuffer(pointLightBuffer)
        {}

        const char* name() const noexcept override { return "StaticMeshForward"; }
        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Graphics;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.get_texture("FinalColor", m_color);
            if (!result)
            {
                return result;
            }
            result = builder.render(&m_color, 1);
            if (!result)
            {
                return result;
            }
            result = builder.get_view("FinalColorRTV", m_colorRtv);
            if (!result)
            {
                return result;
            }

            RHI::TextureDesc depthDesc{};
            depthDesc.name = "SceneDepth";
            depthDesc.bufferCount = 1;
            depthDesc.kind = RHI::TextureKind::DepthStencil;
            depthDesc.width = builder.width();
            depthDesc.height = builder.height();
            depthDesc.format = RHI::ColorFormat::D24_UNorm_S8_UInt;
            depthDesc.clearDepth = 1.0f;
            depthDesc.clearStencil = 0;
            result = builder.create_texture(depthDesc, m_depth);
            if (!result)
            {
                return result;
            }

            RHI::ViewDesc depthDsvDesc{};
            depthDsvDesc.name = "SceneDepthDSV";
            depthDsvDesc.type = RHI::ViewType::DepthStencil;
            depthDsvDesc.bufferKind = RHI::BufferKind::Texture;
            depthDsvDesc.textureHandle = m_depth;
            depthDsvDesc.colorFormat = RHI::ColorFormat::D24_UNorm_S8_UInt;
            result = builder.create_view(depthDsvDesc, m_depthDsv);
            if (!result)
            {
                return result;
            }

            result = builder.read_buffer(m_renderObjectBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_transformBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_viewProjectionBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_visibleObjectCountBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_materialBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_lightFrameBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_pointLightBuffer);
            if (!result)
            {
                return result;
            }

            result = builder.get_buffer("MeshPool.Position", m_positionBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("MeshPool.Uv", m_uvBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("MeshPool.Normal", m_normalBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("MeshPool.Index", m_indexBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("IndirectCommandBuffer", m_indirectCommandBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer(
                "IndirectCommandCountBuffer", m_indirectCommandCountBuffer);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "StaticMeshForwardRootSignature";
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::_32BitConstants,
                    RHI::ShaderVisibility::All, 1 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 1 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 2 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 3 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 2 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 4 });
            result =
                builder.create_root_signature(rootSignatureDesc, m_rootSignature);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc vsDesc{};
            vsDesc.name = "StaticMeshForwardVS";
            vsDesc.filePath = "Shaders/D3D12/StaticMeshForward.hlsl";
            vsDesc.entryPoint = "vs_main";
            vsDesc.targetProfile = "vs_6_0";
            result = builder.create_shader_blob(vsDesc, m_vertexShader);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc psDesc{};
            psDesc.name = "StaticMeshForwardPS";
            psDesc.filePath = "Shaders/D3D12/StaticMeshForward.hlsl";
            psDesc.entryPoint = "ps_main";
            psDesc.targetProfile = "ps_6_0";
            result = builder.create_shader_blob(psDesc, m_pixelShader);
            if (!result)
            {
                return result;
            }

            RHI::GraphicsPipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "StaticMeshForwardPipeline";
            pipelineDesc.rootSignatureHandle = m_rootSignature;
            pipelineDesc.vsHandle = m_vertexShader;
            pipelineDesc.psHandle = m_pixelShader;
            pipelineDesc.inputElements = {
                { "POSITION", 0, RHI::InputElementFormat::R32G32B32A32_Float, 0, 0 },
                { "TEXCOORD", 0, RHI::InputElementFormat::R32G32_Float, 1, 0 },
                { "NORMAL", 0, RHI::InputElementFormat::R32G32B32_Float, 2, 0 },
            };
            pipelineDesc.rasterizerState.cullMode = RHI::CullMode::None;
            pipelineDesc.depthStencilState.depthEnable = true;
            pipelineDesc.depthStencilState.depthWriteMask =
                RHI::DepthWriteMask::All;
            pipelineDesc.depthStencilState.depthFunc =
                RHI::ComparisonFunc::LessEqual;
            pipelineDesc.dsvFormat = RHI::ColorFormat::D24_UNorm_S8_UInt;
            pipelineDesc.blendMode = { RHI::BlendMode::None };
            pipelineDesc.rtvFormats = { RHI::ColorFormat::R8G8B8A8_UNORM };
            return builder.create_graphics_pipeline(pipelineDesc, m_pipeline);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.use_texture(
                m_color,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::RenderTarget,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }
            result = builder.use_texture(
                m_depth,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::DepthWrite,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_renderObjectBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_transformBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_viewProjectionBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_visibleObjectCountBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_materialBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_lightFrameBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_pointLightBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_positionBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::VertexBuffer,
                RHI::ResourceState::VertexBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_uvBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::VertexBuffer,
                RHI::ResourceState::VertexBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_normalBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::VertexBuffer,
                RHI::ResourceState::VertexBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_indexBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::IndexBuffer,
                RHI::ResourceState::IndexBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_indirectCommandBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::IndirectArgument,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }
            return builder.use_buffer(
                m_indirectCommandCountBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::IndirectArgument,
                RHI::ResourceState::Common);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            static constexpr Math::float4 k_clearColor =
                Math::float4::from_rgba8(63, 63, 63, 255);
            commandContext->clear_render_target(m_colorRtv, k_clearColor.data());
            commandContext->clear_depth_stencil(m_depthDsv, 1.0f, 0);
            commandContext->set_render_targets(&m_colorRtv, 1, m_depthDsv);
            commandContext->set_viewport_scissor(context.width(), context.height());
            commandContext->set_graphics_pipeline(m_pipeline);
            commandContext->set_primitive_topology(
                RHI::PrimitiveTopologyType::Triangle);
            commandContext->set_32bit_constant(0, 0xffffffffu);
            commandContext->set_cbv(1, m_viewProjectionBuffer);
            commandContext->set_srv(2, m_renderObjectBuffer);
            commandContext->set_srv(3, m_transformBuffer);
            commandContext->set_srv(4, m_visibleObjectCountBuffer);
            commandContext->set_srv(5, m_materialBuffer);
            commandContext->set_cbv(6, m_lightFrameBuffer);
            commandContext->set_srv(7, m_pointLightBuffer);
            commandContext->set_vertex_buffer(0, m_positionBuffer);
            commandContext->set_vertex_buffer(1, m_uvBuffer);
            commandContext->set_vertex_buffer(2, m_normalBuffer);
            commandContext->set_index_buffer(m_indexBuffer, RHI::IndexFormat::UInt32);

            const DrawFrameData& frameState =
                m_drawFrameState.frame_state(context.frame_index());
            if (frameState.objectCount == 0)
            {
                return;
            }

            commandContext->execute_indexed_indirect(
                m_indirectCommandBuffer,
                m_indirectCommandCountBuffer,
                frameState.objectCount);
        }

    private:
        const DrawFrameState& m_drawFrameState;
        RHI::TextureHandle m_color{};
        RHI::TextureHandle m_depth{};
        RHI::ViewHandle m_colorRtv{};
        RHI::ViewHandle m_depthDsv{};
        RHI::BufferHandle m_renderObjectBuffer{};
        RHI::BufferHandle m_transformBuffer{};
        RHI::BufferHandle m_viewProjectionBuffer{};
        RHI::BufferHandle m_visibleObjectCountBuffer{};
        RHI::BufferHandle m_materialBuffer{};
        RHI::BufferHandle m_lightFrameBuffer{};
        RHI::BufferHandle m_pointLightBuffer{};
        RHI::BufferHandle m_positionBuffer{};
        RHI::BufferHandle m_uvBuffer{};
        RHI::BufferHandle m_normalBuffer{};
        RHI::BufferHandle m_indexBuffer{};
        RHI::BufferHandle m_indirectCommandBuffer{};
        RHI::BufferHandle m_indirectCommandCountBuffer{};
        RHI::RootSignatureHandle m_rootSignature{};
        RHI::ShaderBlobHandle m_vertexShader{};
        RHI::ShaderBlobHandle m_pixelShader{};
        RHI::PipelineStateHandle m_pipeline{};
    };
}
