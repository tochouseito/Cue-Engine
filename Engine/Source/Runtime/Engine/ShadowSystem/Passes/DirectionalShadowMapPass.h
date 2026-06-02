// DirectionalShadowMapPass の役割と公開要素を定義する

#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <DrawSystem/DrawFrameState.h>
#include <ShadowSystem/GpuData/ShadowData.h>
#include <ShadowSystem/ShadowBindings.h>

// === C++ includes ===
#include <string>

namespace Cue::ShadowSystem
{
    class DirectionalShadowMapPass final : public RHI::FrameGraphPass
    {
    public:
        DirectionalShadowMapPass(
            const DrawSystem::DrawFrameState& a_drawFrameState,
            RHI::BufferHandle a_renderObjectBufferHandle,
            RHI::BufferHandle a_transformBufferHandle,
            RHI::BufferHandle a_visibleObjectCountBufferHandle,
            RHI::BufferHandle a_skinPaletteBufferHandle,
            const ShadowBindings& a_shadowBindings,
            uint32_t a_indexCountPerInstance)
            : m_drawFrameState(a_drawFrameState)
            , m_renderObjectBufferHandle(a_renderObjectBufferHandle)
            , m_transformBufferHandle(a_transformBufferHandle)
            , m_visibleObjectCountBufferHandle(a_visibleObjectCountBufferHandle)
            , m_skinPaletteBufferHandle(a_skinPaletteBufferHandle)
            , m_shadowBindings(a_shadowBindings)
            , m_indexCountPerInstance(a_indexCountPerInstance)
        {}

        const char* name() const noexcept override
        {
            return "DirectionalShadowMap";
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
            shadowMapDesc.name = "DirectionalShadowMap";
            shadowMapDesc.bufferCount = 1;
            shadowMapDesc.kind = RHI::TextureKind::DepthStencil;
            shadowMapDesc.width = GpuData::k_directionalShadowMapSize;
            shadowMapDesc.height = GpuData::k_directionalShadowMapSize;
            shadowMapDesc.format = RHI::ColorFormat::D24_UNorm_S8_UInt;
            shadowMapDesc.clearDepth = 1.0f;
            Result result = builder.create_texture(shadowMapDesc, m_shadowMapHandle);
            if (!result)
            {
                return result;
            }

            RHI::ViewDesc shadowDsvDesc{};
            shadowDsvDesc.name = "DirectionalShadowMapDSV";
            shadowDsvDesc.type = RHI::ViewType::DepthStencil;
            shadowDsvDesc.bufferKind = RHI::BufferKind::Texture;
            shadowDsvDesc.textureHandle = m_shadowMapHandle;
            shadowDsvDesc.colorFormat = RHI::ColorFormat::D24_UNorm_S8_UInt;
            result = builder.create_view(shadowDsvDesc, m_shadowMapDsvHandle);
            if (!result)
            {
                return result;
            }

            RHI::ViewDesc shadowSrvDesc{};
            shadowSrvDesc.name = "DirectionalShadowMapSRV";
            shadowSrvDesc.type = RHI::ViewType::ShaderResourceTexture2D;
            shadowSrvDesc.bufferKind = RHI::BufferKind::Texture;
            shadowSrvDesc.textureHandle = m_shadowMapHandle;
            shadowSrvDesc.colorFormat = RHI::ColorFormat::R24_UNorm_X8_Typeless;
            result = builder.create_view(shadowSrvDesc, m_shadowMapSrvHandle);
            if (!result)
            {
                return result;
            }

            result = builder.read_buffer(m_shadowBindings.directionalShadowFrameBuffer);
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
            result = builder.read_buffer(m_visibleObjectCountBufferHandle);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_skinPaletteBufferHandle);
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
            result = builder.get_buffer(
                "StaticMeshPool.SkinInfluence",
                m_influenceBufferHandle);
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

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "DirectionalShadowMapRootSignature";
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::_32BitConstants,
                RHI::ShaderVisibility::All,
                1 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 1 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 2 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::Vertex, 3 });
            result =
                builder.create_root_signature(rootSignatureDesc, m_rootSignatureHandle);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc vertexShaderDesc{};
            vertexShaderDesc.name = "DirectionalShadowMapVS";
            vertexShaderDesc.filePath = "Shaders/D3D12/DirectionalShadowMap.hlsl";
            vertexShaderDesc.entryPoint = "vs_main";
            vertexShaderDesc.targetProfile = "vs_6_0";
            result = builder.create_shader_blob(vertexShaderDesc, m_vertexShaderHandle);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc pixelShaderDesc{};
            pixelShaderDesc.name = "DirectionalShadowMapPS";
            pixelShaderDesc.filePath = "Shaders/D3D12/DirectionalShadowMap.hlsl";
            pixelShaderDesc.entryPoint = "ps_main";
            pixelShaderDesc.targetProfile = "ps_6_0";
            result = builder.create_shader_blob(pixelShaderDesc, m_pixelShaderHandle);
            if (!result)
            {
                return result;
            }

            RHI::GraphicsPipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "DirectionalShadowMapPipeline";
            pipelineDesc.rootSignatureHandle = m_rootSignatureHandle;
            pipelineDesc.vsHandle = m_vertexShaderHandle;
            pipelineDesc.psHandle = m_pixelShaderHandle;
            pipelineDesc.inputElements = {
                { "POSITION", 0, RHI::InputElementFormat::R32G32B32A32_Float, 0, 0 },
                { "TEXCOORD", 0, RHI::InputElementFormat::R32G32_Float, 1, 0 },
                { "NORMAL", 0, RHI::InputElementFormat::R32G32B32_Float, 2, 0 },
                { "JOINTS", 0, RHI::InputElementFormat::R32G32B32A32_UInt, 3, 0 },
                { "WEIGHTS", 0, RHI::InputElementFormat::R32G32B32A32_Float, 3, 16 },
            };
            pipelineDesc.rasterizerState.cullMode = RHI::CullMode::None;
            pipelineDesc.rasterizerState.depthBias = 1000;
            pipelineDesc.rasterizerState.slopeScaledDepthBias = 2.0f;
            pipelineDesc.depthStencilState.depthEnable = true;
            pipelineDesc.depthStencilState.depthWriteMask = RHI::DepthWriteMask::All;
            pipelineDesc.depthStencilState.depthFunc = RHI::ComparisonFunc::LessEqual;
            pipelineDesc.dsvFormat = RHI::ColorFormat::D24_UNorm_S8_UInt;
            pipelineDesc.blendMode = {};
            pipelineDesc.rtvFormats = {};
            return builder.create_graphics_pipeline(pipelineDesc, m_pipelineHandle);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.use_texture(
                m_shadowMapHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::DepthWrite,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }

            result = builder.use_buffer(
                m_shadowBindings.directionalShadowFrameBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
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
                m_visibleObjectCountBufferHandle,
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
                m_uvBufferHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_normalBufferHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_influenceBufferHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_skinPaletteBufferHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::Common);
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
            return builder.use_buffer(
                m_indirectCommandCountBufferHandle,
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

            const DrawSystem::DrawFrameData& frameState =
                m_drawFrameState.frame_state(context.frame_index());

            commandContext->clear_depth_stencil(m_shadowMapDsvHandle, 1.0f, 0);
            commandContext->set_render_targets(nullptr, 0, m_shadowMapDsvHandle);
            commandContext->set_viewport_scissor(
                GpuData::k_directionalShadowMapSize,
                GpuData::k_directionalShadowMapSize);
            commandContext->set_graphics_pipeline(m_pipelineHandle);
            commandContext->set_primitive_topology(
                RHI::PrimitiveTopologyType::Triangle);

            commandContext->set_32bit_constant(0, 0xffffffffu);
            commandContext->set_cbv(1, m_shadowBindings.directionalShadowFrameBuffer);
            const RHI::BufferHandle renderObjectBufferHandle =
                (!frameState.useCpuBatching && m_sortedRenderObjectBufferHandle.valid())
                ? m_sortedRenderObjectBufferHandle
                : m_renderObjectBufferHandle;
            commandContext->set_srv(2, renderObjectBufferHandle);
            commandContext->set_srv(3, m_transformBufferHandle);
            commandContext->set_srv(4, m_visibleObjectCountBufferHandle);
            commandContext->set_srv(5, m_skinPaletteBufferHandle);
            commandContext->set_vertex_buffer(0, m_positionBufferHandle);
            commandContext->set_vertex_buffer(1, m_uvBufferHandle);
            commandContext->set_vertex_buffer(2, m_normalBufferHandle);
            commandContext->set_vertex_buffer(3, m_influenceBufferHandle);

            if (frameState.useCpuBatching)
            {
                commandContext->set_index_buffer(
                    m_indexBufferHandle, RHI::IndexFormat::UInt32);
                for (const DrawSystem::CpuIndexedDraw& draw :
                    frameState.cpuIndexedDraws)
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
                commandContext->set_index_buffer(
                    m_indexBufferHandle, RHI::IndexFormat::UInt32);
                commandContext->execute_indexed_indirect(
                    m_indirectCommandBufferHandle,
                    m_indirectCommandCountBufferHandle,
                    frameState.objectCount);
            }
        }

    private:
        const DrawSystem::DrawFrameState& m_drawFrameState;
        RHI::BufferHandle m_renderObjectBufferHandle{};
        RHI::BufferHandle m_sortedRenderObjectBufferHandle{};
        RHI::BufferHandle m_transformBufferHandle{};
        RHI::BufferHandle m_visibleObjectCountBufferHandle{};
        RHI::BufferHandle m_skinPaletteBufferHandle{};
        RHI::BufferHandle m_positionBufferHandle{};
        RHI::BufferHandle m_uvBufferHandle{};
        RHI::BufferHandle m_normalBufferHandle{};
        RHI::BufferHandle m_influenceBufferHandle{};
        RHI::BufferHandle m_indexBufferHandle{};
        RHI::BufferHandle m_indirectCommandBufferHandle{};
        RHI::BufferHandle m_indirectCommandCountBufferHandle{};
        ShadowBindings m_shadowBindings{};
        uint32_t m_indexCountPerInstance = 0;
        RHI::TextureHandle m_shadowMapHandle{};
        RHI::ViewHandle m_shadowMapDsvHandle{};
        RHI::ViewHandle m_shadowMapSrvHandle{};
        RHI::RootSignatureHandle m_rootSignatureHandle{};
        RHI::ShaderBlobHandle m_vertexShaderHandle{};
        RHI::ShaderBlobHandle m_pixelShaderHandle{};
        RHI::PipelineStateHandle m_pipelineHandle{};
    };
}
