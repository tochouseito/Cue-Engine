#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <GameCore/RenderSceneState.h>

// === C++ includes ===
#include <string>

namespace Cue
{
    class ShadowMapPass final : public RHI::FrameGraphPass
    {
    public:
        ShadowMapPass(
            const RenderSceneState& a_renderSceneState,
            RHI::BufferHandle a_renderObjectBufferHandle,
            RHI::BufferHandle a_transformBufferHandle,
            RHI::BufferHandle a_shadowMappingBufferHandle,
            RHI::BufferHandle a_visibleObjectCountBufferHandle,
            uint32_t a_indexCountPerInstance)
            : m_renderSceneState(a_renderSceneState)
            , m_renderObjectBufferHandle(a_renderObjectBufferHandle)
            , m_transformBufferHandle(a_transformBufferHandle)
            , m_shadowMappingBufferHandle(a_shadowMappingBufferHandle)
            , m_visibleObjectCountBufferHandle(a_visibleObjectCountBufferHandle)
            , m_indexCountPerInstance(a_indexCountPerInstance)
        {}

        const char* name() const noexcept override
        {
            return "ShadowMap";
        }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Graphics;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            m_sortedRenderObjectBufferHandle = {};
            Result sortedBufferResult = builder.get_buffer(
                "SortedRenderObjectBuffer",
                m_sortedRenderObjectBufferHandle);
            if (!sortedBufferResult && sortedBufferResult.code != Code::NotFound)
            {
                return sortedBufferResult;
            }

            RHI::TextureDesc shadowMapDesc{};
            shadowMapDesc.name = "ShadowMap";
            shadowMapDesc.bufferCount = 1;
            shadowMapDesc.kind = RHI::TextureKind::RenderTarget;
            shadowMapDesc.width = k_shadowMapSize;
            shadowMapDesc.height = k_shadowMapSize;
            shadowMapDesc.format = RHI::ColorFormat::R32_FLOAT;
            shadowMapDesc.clearColor[0] = 1.0f;
            shadowMapDesc.clearColor[1] = 1.0f;
            shadowMapDesc.clearColor[2] = 1.0f;
            shadowMapDesc.clearColor[3] = 1.0f;
            Result result = builder.create_texture(shadowMapDesc, m_shadowMapHandle);
            if (!result)
            {
                return result;
            }
            result = builder.render(&m_shadowMapHandle, 1);
            if (!result)
            {
                return result;
            }

            RHI::ViewDesc shadowMapRtvDesc{};
            shadowMapRtvDesc.name = "ShadowMapRTV";
            shadowMapRtvDesc.type = RHI::ViewType::RenderTarget;
            shadowMapRtvDesc.bufferKind = RHI::BufferKind::Texture;
            shadowMapRtvDesc.textureHandle = m_shadowMapHandle;
            shadowMapRtvDesc.colorFormat = RHI::ColorFormat::R32_FLOAT;
            result = builder.create_view(shadowMapRtvDesc, m_shadowMapRtvHandle);
            if (!result)
            {
                return result;
            }

            RHI::ViewDesc shadowMapSrvDesc{};
            shadowMapSrvDesc.name = "ShadowMapSRV";
            shadowMapSrvDesc.type = RHI::ViewType::ShaderResourceTexture2D;
            shadowMapSrvDesc.bufferKind = RHI::BufferKind::Texture;
            shadowMapSrvDesc.textureHandle = m_shadowMapHandle;
            shadowMapSrvDesc.colorFormat = RHI::ColorFormat::R32_FLOAT;
            result = builder.create_view(shadowMapSrvDesc, m_shadowMapSrvHandle);
            if (!result)
            {
                return result;
            }

            RHI::TextureDesc shadowDepthDesc{};
            shadowDepthDesc.name = "ShadowMapDepth";
            shadowDepthDesc.bufferCount = 1;
            shadowDepthDesc.kind = RHI::TextureKind::DepthStencil;
            shadowDepthDesc.width = k_shadowMapSize;
            shadowDepthDesc.height = k_shadowMapSize;
            shadowDepthDesc.format = RHI::ColorFormat::D24_UNorm_S8_UInt;
            shadowDepthDesc.clearDepth = 1.0f;
            result = builder.create_texture(shadowDepthDesc, m_shadowDepthHandle);
            if (!result)
            {
                return result;
            }

            RHI::ViewDesc shadowDepthDsvDesc{};
            shadowDepthDsvDesc.name = "ShadowMapDepthDSV";
            shadowDepthDsvDesc.type = RHI::ViewType::DepthStencil;
            shadowDepthDsvDesc.bufferKind = RHI::BufferKind::Texture;
            shadowDepthDsvDesc.textureHandle = m_shadowDepthHandle;
            shadowDepthDsvDesc.colorFormat = RHI::ColorFormat::D24_UNorm_S8_UInt;
            result = builder.create_view(shadowDepthDsvDesc, m_shadowDepthDsvHandle);
            if (!result)
            {
                return result;
            }

            result = builder.read_buffer(m_renderObjectBufferHandle);
            if (!result)
            {
                return result;
            }
            if (m_sortedRenderObjectBufferHandle.valid())
            {
                result = builder.read_buffer(m_sortedRenderObjectBufferHandle);
                if (!result)
                {
                    return result;
                }
            }
            result = builder.read_buffer(m_transformBufferHandle);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_shadowMappingBufferHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("StaticMeshPool.Position", m_positionBufferHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("StaticMeshPool.Index", m_indexBufferHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer(
                "IndirectCommandBuffer", m_indirectCommandBufferHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer(
                "IndirectCommandCountBuffer", m_indirectCommandCountBufferHandle);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_visibleObjectCountBufferHandle);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "ShadowMapRootSignature";
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::_32BitConstants,
                RHI::ShaderVisibility::All,
                1 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::CBV,
                RHI::ShaderVisibility::All,
                0 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV,
                RHI::ShaderVisibility::All,
                0 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV,
                RHI::ShaderVisibility::All,
                1 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV,
                RHI::ShaderVisibility::All,
                2 });
            result =
                builder.create_root_signature(rootSignatureDesc, m_rootSignatureHandle);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc vertexShaderDesc{};
            vertexShaderDesc.name = "ShadowMapVS";
            vertexShaderDesc.filePath = "Shaders/D3D12/StaticMeshShadow.hlsl";
            vertexShaderDesc.entryPoint = "vs_main";
            vertexShaderDesc.targetProfile = "vs_6_0";
            result = builder.create_shader_blob(vertexShaderDesc, m_vertexShaderHandle);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc pixelShaderDesc{};
            pixelShaderDesc.name = "ShadowMapPS";
            pixelShaderDesc.filePath = "Shaders/D3D12/StaticMeshShadow.hlsl";
            pixelShaderDesc.entryPoint = "ps_main";
            pixelShaderDesc.targetProfile = "ps_6_0";
            result = builder.create_shader_blob(pixelShaderDesc, m_pixelShaderHandle);
            if (!result)
            {
                return result;
            }

            RHI::GraphicsPipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "ShadowMapPipeline";
            pipelineDesc.rootSignatureHandle = m_rootSignatureHandle;
            pipelineDesc.vsHandle = m_vertexShaderHandle;
            pipelineDesc.psHandle = m_pixelShaderHandle;
            pipelineDesc.inputElements = {
                { "POSITION", 0, RHI::InputElementFormat::R32G32B32A32_Float, 0, 0 },
            };
            pipelineDesc.rasterizerState.cullMode = RHI::CullMode::None;
            pipelineDesc.depthStencilState.depthEnable = true;
            pipelineDesc.depthStencilState.depthWriteMask = RHI::DepthWriteMask::All;
            pipelineDesc.depthStencilState.depthFunc = RHI::ComparisonFunc::LessEqual;
            pipelineDesc.dsvFormat = RHI::ColorFormat::D24_UNorm_S8_UInt;
            pipelineDesc.blendMode = { RHI::BlendMode::None };
            pipelineDesc.rtvFormats = { RHI::ColorFormat::R32_FLOAT };
            return builder.create_graphics_pipeline(pipelineDesc, m_pipelineHandle);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.use_texture(
                m_shadowMapHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::RenderTarget,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }

            result = builder.use_texture(
                m_shadowDepthHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::DepthWrite,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }

            result = builder.use_buffer(
                m_renderObjectBufferHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }
            if (m_sortedRenderObjectBufferHandle.valid())
            {
                result = builder.use_buffer(
                    m_sortedRenderObjectBufferHandle,
                    RHI::ResourceAccessType::Read,
                    RHI::ResourceState::ShaderResource,
                    RHI::ResourceState::ShaderResource);
                if (!result)
                {
                    return result;
                }
            }
            result = builder.use_buffer(
                m_transformBufferHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_shadowMappingBufferHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_positionBufferHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_indexBufferHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_indirectCommandBufferHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::IndirectArgument,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_indirectCommandCountBufferHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::IndirectArgument,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }
            return builder.use_buffer(
                m_visibleObjectCountBufferHandle,
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

            constexpr float k_clearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
            commandContext->clear_render_target(m_shadowMapRtvHandle, k_clearColor);
            commandContext->clear_depth_stencil(m_shadowDepthDsvHandle, 1.0f, 0);
            commandContext->set_render_targets(
                &m_shadowMapRtvHandle,
                1,
                m_shadowDepthDsvHandle);
            commandContext->set_viewport_scissor(k_shadowMapSize, k_shadowMapSize);
            commandContext->set_graphics_pipeline(m_pipelineHandle);
            commandContext->set_primitive_topology(
                RHI::PrimitiveTopologyType::Triangle);
            commandContext->set_32bit_constant(0, 0xffffffffu);
            commandContext->set_cbv(1, m_shadowMappingBufferHandle);

            const RenderFrameState& frameState =
                m_renderSceneState.frame_state(context.frame_index());
            const RHI::BufferHandle renderObjectBufferHandle =
                (!frameState.useCpuBatching && m_sortedRenderObjectBufferHandle.valid())
                ? m_sortedRenderObjectBufferHandle
                : m_renderObjectBufferHandle;
            commandContext->set_srv(2, renderObjectBufferHandle);
            commandContext->set_srv(3, m_transformBufferHandle);
            commandContext->set_srv(4, m_visibleObjectCountBufferHandle);
            commandContext->set_vertex_buffer(0, m_positionBufferHandle);
            commandContext->set_index_buffer(m_indexBufferHandle, RHI::IndexFormat::UInt32);

            if (frameState.useCpuBatching)
            {
                for (const CpuIndexedDraw& draw : frameState.cpuIndexedDraws)
                {
                    if (draw.indexCount == 0)
                    {
                        continue;
                    }

                    commandContext->set_32bit_constant(0, draw.renderObjectId);
                    commandContext->draw_indexed_instanced(
                        draw.indexCount, 1, draw.startIndex, draw.baseVertex, 0);
                }
            }
            else if (m_indexCountPerInstance > 0 && frameState.objectCount > 0)
            {
                commandContext->execute_indexed_indirect(
                    m_indirectCommandBufferHandle,
                    m_indirectCommandCountBufferHandle,
                    frameState.objectCount);
            }
        }

    private:
        static constexpr uint32_t k_shadowMapSize = 1024;

        const RenderSceneState& m_renderSceneState;
        uint32_t m_indexCountPerInstance = 0;
        RHI::TextureHandle m_shadowMapHandle{};
        RHI::TextureHandle m_shadowDepthHandle{};
        RHI::ViewHandle m_shadowMapRtvHandle{};
        RHI::ViewHandle m_shadowMapSrvHandle{};
        RHI::ViewHandle m_shadowDepthDsvHandle{};
        RHI::BufferHandle m_renderObjectBufferHandle{};
        RHI::BufferHandle m_sortedRenderObjectBufferHandle{};
        RHI::BufferHandle m_transformBufferHandle{};
        RHI::BufferHandle m_shadowMappingBufferHandle{};
        RHI::BufferHandle m_visibleObjectCountBufferHandle{};
        RHI::BufferHandle m_positionBufferHandle{};
        RHI::BufferHandle m_indexBufferHandle{};
        RHI::BufferHandle m_indirectCommandBufferHandle{};
        RHI::BufferHandle m_indirectCommandCountBufferHandle{};
        RHI::RootSignatureHandle m_rootSignatureHandle{};
        RHI::ShaderBlobHandle m_vertexShaderHandle{};
        RHI::ShaderBlobHandle m_pixelShaderHandle{};
        RHI::PipelineStateHandle m_pipelineHandle{};
    };
} // namespace Cue
