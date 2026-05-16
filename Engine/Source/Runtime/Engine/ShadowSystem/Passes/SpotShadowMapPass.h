#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <DrawSystem/DrawFrameState.h>
#include <ShadowSystem/GpuData/ShadowData.h>
#include <ShadowSystem/ShadowBindings.h>
#include <ShadowSystem/ShadowFrameState.h>

// === C++ includes ===
#include <array>
#include <chrono>
#include <cmath>
#include <string>
#include <utility>

namespace Cue::ShadowSystem
{
    class SpotShadowMapPass final : public RHI::FrameGraphPass
    {
    public:
        SpotShadowMapPass(
            const DrawSystem::DrawFrameState& a_drawFrameState,
            const ShadowFrameState& a_shadowFrameState,
            RHI::BufferHandle a_renderObjectBufferHandle,
            RHI::BufferHandle a_transformBufferHandle,
            RHI::BufferHandle a_visibleObjectCountBufferHandle,
            RHI::BufferHandle a_skinPaletteBufferHandle,
            const ShadowBindings& a_shadowBindings,
            uint32_t a_indexCountPerInstance)
            : m_drawFrameState(a_drawFrameState)
            , m_shadowFrameState(a_shadowFrameState)
            , m_renderObjectBufferHandle(a_renderObjectBufferHandle)
            , m_transformBufferHandle(a_transformBufferHandle)
            , m_visibleObjectCountBufferHandle(a_visibleObjectCountBufferHandle)
            , m_skinPaletteBufferHandle(a_skinPaletteBufferHandle)
            , m_shadowBindings(a_shadowBindings)
            , m_indexCountPerInstance(a_indexCountPerInstance)
        {}

        const char* name() const noexcept override { return "SpotShadowMap"; }

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
            shadowMapDesc.name = "SpotShadowMap";
            shadowMapDesc.bufferCount = 1;
            shadowMapDesc.kind = RHI::TextureKind::DepthStencil;
            shadowMapDesc.width = GpuData::k_spotShadowMapSize;
            shadowMapDesc.height = GpuData::k_spotShadowMapSize;
            shadowMapDesc.format = RHI::ColorFormat::D24_UNorm_S8_UInt;
            shadowMapDesc.clearDepth = 1.0f;
            Result result = builder.create_texture(shadowMapDesc, m_shadowMapHandle);
            if (!result)
            {
                return result;
            }

            RHI::ViewDesc shadowDsvDesc{};
            shadowDsvDesc.name = "SpotShadowMapDSV";
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
            shadowSrvDesc.name = "SpotShadowMapSRV";
            shadowSrvDesc.type = RHI::ViewType::ShaderResourceTexture2D;
            shadowSrvDesc.bufferKind = RHI::BufferKind::Texture;
            shadowSrvDesc.textureHandle = m_shadowMapHandle;
            shadowSrvDesc.colorFormat = RHI::ColorFormat::R24_UNorm_X8_Typeless;
            result = builder.create_view(shadowSrvDesc, m_shadowMapSrvHandle);
            if (!result)
            {
                return result;
            }

            result = builder.read_buffer(m_shadowBindings.spotShadowFrameBuffer);
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
            rootSignatureDesc.name = "SpotShadowMapRootSignature";
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::_32BitConstants,
                RHI::ShaderVisibility::All,
                1 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::_32BitConstants,
                RHI::ShaderVisibility::All,
                2 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 1 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 2 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 3 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::Vertex, 4 });
            result =
                builder.create_root_signature(rootSignatureDesc, m_rootSignatureHandle);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc vertexShaderDesc{};
            vertexShaderDesc.name = "SpotShadowMapVS";
            vertexShaderDesc.filePath = "Shaders/D3D12/SpotShadowMap.hlsl";
            vertexShaderDesc.entryPoint = "vs_main";
            vertexShaderDesc.targetProfile = "vs_6_0";
            result = builder.create_shader_blob(vertexShaderDesc, m_vertexShaderHandle);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc pixelShaderDesc{};
            pixelShaderDesc.name = "SpotShadowMapPS";
            pixelShaderDesc.filePath = "Shaders/D3D12/SpotShadowMap.hlsl";
            pixelShaderDesc.entryPoint = "ps_main";
            pixelShaderDesc.targetProfile = "ps_6_0";
            result = builder.create_shader_blob(pixelShaderDesc, m_pixelShaderHandle);
            if (!result)
            {
                return result;
            }

            RHI::GraphicsPipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "SpotShadowMapPipeline";
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
                m_shadowBindings.spotShadowFrameBuffer,
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
            const ShadowFrameData& shadowFrameState =
                m_shadowFrameState.frame_state(context.frame_index());

            commandContext->clear_depth_stencil(m_shadowMapDsvHandle, 1.0f, 0);
            commandContext->set_render_targets(nullptr, 0, m_shadowMapDsvHandle);
            commandContext->set_viewport_scissor(
                GpuData::k_spotShadowMapSize,
                GpuData::k_spotShadowMapSize);
            commandContext->set_graphics_pipeline(m_pipelineHandle);
            commandContext->set_primitive_topology(
                RHI::PrimitiveTopologyType::Triangle);

            commandContext->set_32bit_constant(0, 0xffffffffu);
            const RHI::BufferHandle renderObjectBufferHandle =
                (!frameState.useCpuBatching && m_sortedRenderObjectBufferHandle.valid())
                ? m_sortedRenderObjectBufferHandle
                : m_renderObjectBufferHandle;
            commandContext->set_srv(2, renderObjectBufferHandle);
            commandContext->set_srv(3, m_transformBufferHandle);
            commandContext->set_srv(4, m_visibleObjectCountBufferHandle);
            commandContext->set_srv(5, m_shadowBindings.spotShadowFrameBuffer);
            commandContext->set_srv(6, m_skinPaletteBufferHandle);
            commandContext->set_vertex_buffer(0, m_positionBufferHandle);
            commandContext->set_vertex_buffer(1, m_uvBufferHandle);
            commandContext->set_vertex_buffer(2, m_normalBufferHandle);
            commandContext->set_vertex_buffer(3, m_influenceBufferHandle);

            const auto drawShadowCasters =
                [&](const GpuData::SpotShadowFrameGpu& a_shadowFrame)
            {
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
                        if (!is_shadow_caster_visible(
                                draw.renderObjectId,
                                a_shadowFrame,
                                frameState.cpuShadowCasters))
                        {
                            continue;
                        }

                        commandContext->set_32bit_constant(0, draw.renderObjectId);
                        commandContext->draw_indexed_instanced(
                            draw.indexCount,
                            1,
                            draw.startIndex,
                            draw.baseVertex,
                            0);
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
            };

            for (uint32_t shadowIndex = 0;
                 shadowIndex < GpuData::k_maxSpotShadowCount;
                 ++shadowIndex)
            {
                const uint32_t tileX =
                    shadowIndex % GpuData::k_spotShadowAtlasColumnCount;
                const uint32_t tileY =
                    shadowIndex / GpuData::k_spotShadowAtlasColumnCount;
                commandContext->set_viewport_scissor(
                    tileX * GpuData::k_spotShadowTileSize,
                    tileY * GpuData::k_spotShadowTileSize,
                    GpuData::k_spotShadowTileSize,
                    GpuData::k_spotShadowTileSize);
                commandContext->set_32bit_constant(1, shadowIndex);
                drawShadowCasters(shadowFrameState.spotShadows[shadowIndex]);
            }
        }

    private:
        [[nodiscard]] static Math::float4 transform_point(
            const Math::float3& a_position,
            const Math::float4x4& a_matrix) noexcept
        {
            return Math::float4(
                a_position.x * a_matrix.values[0][0] +
                    a_position.y * a_matrix.values[1][0] +
                    a_position.z * a_matrix.values[2][0] +
                    a_matrix.values[3][0],
                a_position.x * a_matrix.values[0][1] +
                    a_position.y * a_matrix.values[1][1] +
                    a_position.z * a_matrix.values[2][1] +
                    a_matrix.values[3][1],
                a_position.x * a_matrix.values[0][2] +
                    a_position.y * a_matrix.values[1][2] +
                    a_position.z * a_matrix.values[2][2] +
                    a_matrix.values[3][2],
                a_position.x * a_matrix.values[0][3] +
                    a_position.y * a_matrix.values[1][3] +
                    a_position.z * a_matrix.values[2][3] +
                    a_matrix.values[3][3]);
        }

        [[nodiscard]] static bool is_shadow_caster_visible(
            uint32_t a_renderObjectId,
            const GpuData::SpotShadowFrameGpu& a_shadowFrame,
            const std::vector<DrawSystem::CpuShadowCaster>& a_casters) noexcept
        {
            if (a_shadowFrame.params.x < 0.5f)
            {
                return false;
            }
            if (a_renderObjectId >= a_casters.size())
            {
                return true;
            }

            const DrawSystem::CpuShadowCaster& caster =
                a_casters[a_renderObjectId];
            const Math::float4 lightPosition =
                transform_point(caster.center, a_shadowFrame.view);
            const Math::float4 clipPosition =
                Math::float4(
                    lightPosition.x,
                    lightPosition.y,
                    lightPosition.z,
                    1.0f);
            const Math::float4 clip =
                Math::float4(
                    clipPosition.x * a_shadowFrame.projection.values[0][0] +
                        clipPosition.y * a_shadowFrame.projection.values[1][0] +
                        clipPosition.z * a_shadowFrame.projection.values[2][0] +
                        clipPosition.w * a_shadowFrame.projection.values[3][0],
                    clipPosition.x * a_shadowFrame.projection.values[0][1] +
                        clipPosition.y * a_shadowFrame.projection.values[1][1] +
                        clipPosition.z * a_shadowFrame.projection.values[2][1] +
                        clipPosition.w * a_shadowFrame.projection.values[3][1],
                    clipPosition.x * a_shadowFrame.projection.values[0][2] +
                        clipPosition.y * a_shadowFrame.projection.values[1][2] +
                        clipPosition.z * a_shadowFrame.projection.values[2][2] +
                        clipPosition.w * a_shadowFrame.projection.values[3][2],
                    clipPosition.x * a_shadowFrame.projection.values[0][3] +
                        clipPosition.y * a_shadowFrame.projection.values[1][3] +
                        clipPosition.z * a_shadowFrame.projection.values[2][3] +
                        clipPosition.w * a_shadowFrame.projection.values[3][3]);
            if (clip.w <= 0.0001f)
            {
                return false;
            }

            const float invW = 1.0f / clip.w;
            const float margin = (std::min)(
                caster.radius / (std::max)(std::abs(lightPosition.z), 0.001f),
                1.0f);
            const float ndcX = clip.x * invW;
            const float ndcY = clip.y * invW;
            const float ndcZ = clip.z * invW;
            return ndcX >= -1.0f - margin &&
                ndcX <= 1.0f + margin &&
                ndcY >= -1.0f - margin &&
                ndcY <= 1.0f + margin &&
                ndcZ >= -1.0f - margin &&
                ndcZ <= 1.0f + margin;
        }

        const DrawSystem::DrawFrameState& m_drawFrameState;
        const ShadowFrameState& m_shadowFrameState;
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
