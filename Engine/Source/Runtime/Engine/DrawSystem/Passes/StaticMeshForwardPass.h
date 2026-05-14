#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <DrawSystem/DebugViewShadingMode.h>
#include <DrawSystem/DrawFrameState.h>
#include <LightingSystem/LightingBindings.h>
#include <ShadowSystem/ShadowBindings.h>

// === C++ includes ===
#include <array>
#include <chrono>
#include <string>
#include <utility>

namespace Cue::DrawSystem
{
    class StaticMeshForwardPass final : public RHI::FrameGraphPass
    {
    public:
        StaticMeshForwardPass(std::string a_name,
            std::string a_colorName,
            std::string a_colorRtvName,
            std::string a_depthName,
            std::string a_depthDsvName,
            const DrawFrameState& a_drawFrameState,
            RHI::BufferHandle a_renderObjectBufferHandle,
            RHI::BufferHandle a_transformBufferHandle,
            RHI::BufferHandle a_viewProjectionBufferHandle,
            RHI::BufferHandle a_visibleObjectCountBufferHandle,
            RHI::BufferHandle a_materialBufferHandle,
            const LightingSystem::LightingBindings& a_lightingBindings,
            const ShadowSystem::ShadowBindings& a_shadowBindings,
            uint32_t a_indexCountPerInstance,
            const RHI::ViewHandle& a_reflectionSkyboxSrvHandle,
            const DebugViewShadingMode* a_shadingMode = nullptr)
            : m_name(std::move(a_name)),
            m_colorName(std::move(a_colorName)),
            m_colorRtvName(std::move(a_colorRtvName)),
            m_depthName(std::move(a_depthName)),
            m_depthDsvName(std::move(a_depthDsvName)),
            m_drawFrameState(a_drawFrameState),
            m_renderObjectBufferHandle(a_renderObjectBufferHandle),
            m_transformBufferHandle(a_transformBufferHandle),
            m_viewProjectionBufferHandle(a_viewProjectionBufferHandle),
            m_visibleObjectCountBufferHandle(a_visibleObjectCountBufferHandle),
            m_materialBufferHandle(a_materialBufferHandle),
            m_lightingBindings(a_lightingBindings),
            m_shadowBindings(a_shadowBindings),
            m_reflectionSkyboxSrvHandle(a_reflectionSkyboxSrvHandle),
            m_shadingMode(a_shadingMode),
            m_indexCountPerInstance(a_indexCountPerInstance)
        {}

        const char* name() const noexcept override { return m_name.c_str(); }

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

            Result result = builder.get_texture(m_colorName, m_colorHandle);
            if (!result)
            {
                return result;
            }
            result = builder.render(&m_colorHandle, 1);
            if (!result)
            {
                return result;
            }
            result = builder.get_view(m_colorRtvName, m_colorRtvHandle);
            if (!result)
            {
                return result;
            }

            RHI::TextureDesc sceneDepthDesc{};
            sceneDepthDesc.name = m_depthName;
            sceneDepthDesc.bufferCount = 1;
            sceneDepthDesc.kind = RHI::TextureKind::DepthStencil;
            sceneDepthDesc.width = builder.width();
            sceneDepthDesc.height = builder.height();
            sceneDepthDesc.format = RHI::ColorFormat::D24_UNorm_S8_UInt;
            sceneDepthDesc.clearDepth = 1.0f;
            sceneDepthDesc.clearStencil = 0;
            result = builder.create_texture(sceneDepthDesc, m_depthHandle);
            if (!result)
            {
                return result;
            }

            RHI::ViewDesc sceneDepthDsvDesc{};
            sceneDepthDsvDesc.name = m_depthDsvName;
            sceneDepthDsvDesc.type = RHI::ViewType::DepthStencil;
            sceneDepthDsvDesc.bufferKind = RHI::BufferKind::Texture;
            sceneDepthDsvDesc.textureHandle = m_depthHandle;
            sceneDepthDsvDesc.colorFormat = RHI::ColorFormat::D24_UNorm_S8_UInt;
            result = builder.create_view(sceneDepthDsvDesc, m_depthDsvHandle);
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
            result = builder.read_buffer(m_viewProjectionBufferHandle);
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
            result = builder.read_buffer(m_materialBufferHandle);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_lightingBindings.frameBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_lightingBindings.directionalLightBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_lightingBindings.pointLightBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_lightingBindings.spotLightBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_shadowBindings.spotShadowFrameBuffer);
            if (!result)
            {
                return result;
            }
            result =
                builder.read_buffer(m_shadowBindings.directionalShadowFrameBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_shadowBindings.pointShadowFaceBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.get_texture(
                "DirectionalShadowMap", m_directionalShadowMapHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_view(
                "DirectionalShadowMapSRV", m_directionalShadowMapSrvHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_texture("PointShadowMap", m_pointShadowMapHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_view("PointShadowMapSRV", m_pointShadowMapSrvHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_texture("SpotShadowMap", m_spotShadowMapHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_view("SpotShadowMapSRV", m_spotShadowMapSrvHandle);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "StaticMeshForwardRootSignature";
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::_32BitConstants,
                RHI::ShaderVisibility::All,
                1 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::DescriptorTableSRV,
                RHI::ShaderVisibility::Pixel,
                0,
                0,
                1 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 1 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 2 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 3 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 2 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 4 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 5 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 6 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 7 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::DescriptorTableSRV,
                RHI::ShaderVisibility::Pixel,
                8,
                1,
                0 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 3 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::DescriptorTableSRV,
                RHI::ShaderVisibility::Pixel,
                9,
                1,
                0 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 10 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::DescriptorTableSRV,
                RHI::ShaderVisibility::Pixel,
                11,
                1,
                0 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::_32BitConstants,
                RHI::ShaderVisibility::Pixel,
                4 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::DescriptorTableSRV,
                RHI::ShaderVisibility::Pixel,
                12,
                1,
                0 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::_32BitConstants,
                RHI::ShaderVisibility::Pixel,
                5 });
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
            pipelineDesc.inputElements = {
                { "POSITION", 0, RHI::InputElementFormat::R32G32B32A32_Float, 0, 0 },
                { "TEXCOORD", 0, RHI::InputElementFormat::R32G32_Float, 1, 0 },
                { "NORMAL", 0, RHI::InputElementFormat::R32G32B32_Float, 2, 0 },
            };
            pipelineDesc.rasterizerState.cullMode = RHI::CullMode::None;
            pipelineDesc.depthStencilState.depthEnable = true;
            pipelineDesc.depthStencilState.depthWriteMask = RHI::DepthWriteMask::All;
            pipelineDesc.depthStencilState.depthFunc = RHI::ComparisonFunc::LessEqual;
            pipelineDesc.dsvFormat = RHI::ColorFormat::D24_UNorm_S8_UInt;
            pipelineDesc.blendMode = { RHI::BlendMode::None };
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
                m_viewProjectionBufferHandle,
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
                m_indexBufferHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }

            result = builder.use_buffer(
                m_meshRangeBufferHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_materialBufferHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_lightingBindings.frameBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_lightingBindings.directionalLightBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_lightingBindings.pointLightBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_lightingBindings.spotLightBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::Common);
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
                m_shadowBindings.directionalShadowFrameBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_shadowBindings.pointShadowFaceBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }
            result = builder.use_texture(
                m_directionalShadowMapHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_texture(
                m_pointShadowMapHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_texture(
                m_spotShadowMapHandle,
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
            using Clock = std::chrono::steady_clock;
            auto ms_since =
                [](const Clock::time_point& a_start, const Clock::time_point& a_end)
                {
                    return std::chrono::duration<double, std::milli>(
                        a_end - a_start)
                        .count();
                };

            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            auto* passStats =
                static_cast<RHI::FrameGraphExecutionStats::PassExecutionStats*>(
                    context.pass_stats());
            if (passStats != nullptr)
            {
                passStats->detailTimings.clear();
            }

            auto add_detail_timing =
                [&](const char* a_label,
                    const Clock::time_point& a_start,
                    const Clock::time_point& a_end)
                {
                    if (passStats == nullptr)
                    {
                        return;
                    }

                    passStats->detailTimings.push_back(
                        RHI::FrameGraphExecutionStats::PassExecutionStats::
                            DetailTiming{
                                a_label,
                                ms_since(a_start, a_end) });
                };

            const DrawFrameData& frameState =
                m_drawFrameState.frame_state(context.frame_index());

            const Clock::time_point clearStartTime = Clock::now();
            commandContext->clear_render_target(m_colorRtvHandle,
                k_clearColorVec.data());
            commandContext->clear_depth_stencil(m_depthDsvHandle, 1.0f, 0);
            add_detail_timing("clear", clearStartTime, Clock::now());

            const Clock::time_point targetSetupStartTime = Clock::now();
            commandContext->set_render_targets(
                &m_colorRtvHandle,
                1,
                m_depthDsvHandle);
            commandContext->set_viewport_scissor(context.width(), context.height());
            add_detail_timing("targets_viewport", targetSetupStartTime, Clock::now());

            const Clock::time_point pipelineSetupStartTime = Clock::now();
            commandContext->set_graphics_pipeline(m_pipelineHandle);
            commandContext->set_primitive_topology(
                RHI::PrimitiveTopologyType::Triangle);
            add_detail_timing("pipeline_topology", pipelineSetupStartTime, Clock::now());

            const Clock::time_point bindingStartTime = Clock::now();
            commandContext->set_32bit_constant(0, 0xffffffffu);
            commandContext->set_cbv(1, m_viewProjectionBufferHandle);
            commandContext->set_graphics_texture_table(2);
            const RHI::BufferHandle renderObjectBufferHandle =
                (!frameState.useCpuBatching && m_sortedRenderObjectBufferHandle.valid())
                ? m_sortedRenderObjectBufferHandle
                : m_renderObjectBufferHandle;
            commandContext->set_srv(3, renderObjectBufferHandle);
            commandContext->set_srv(4, m_transformBufferHandle);
            commandContext->set_srv(5, m_visibleObjectCountBufferHandle);
            commandContext->set_srv(6, m_materialBufferHandle);
            commandContext->set_cbv(7, m_lightingBindings.frameBuffer);
            commandContext->set_srv(8, m_lightingBindings.directionalLightBuffer);
            commandContext->set_srv(9, m_lightingBindings.pointLightBuffer);
            commandContext->set_srv(10, m_lightingBindings.spotLightBuffer);
            commandContext->set_srv(11, m_shadowBindings.spotShadowFrameBuffer);
            commandContext->set_graphics_descriptor_table(
                12, m_spotShadowMapSrvHandle);
            commandContext->set_cbv(
                13, m_shadowBindings.directionalShadowFrameBuffer);
            commandContext->set_graphics_descriptor_table(
                14, m_directionalShadowMapSrvHandle);
            commandContext->set_srv(15, m_shadowBindings.pointShadowFaceBuffer);
            commandContext->set_graphics_descriptor_table(
                16, m_pointShadowMapSrvHandle);
            const DebugViewShadingMode shadingMode = m_shadingMode != nullptr
                ? *m_shadingMode
                : DebugViewShadingMode::MaterialLighting;
            commandContext->set_32bit_constant(
                17,
                to_shader_value(shadingMode));
            if (m_reflectionSkyboxSrvHandle.valid())
            {
                commandContext->set_graphics_descriptor_table(
                    18, m_reflectionSkyboxSrvHandle);
            }
            commandContext->set_32bit_constant(
                19,
                m_reflectionSkyboxSrvHandle.valid() ? 1u : 0u);
            commandContext->set_vertex_buffer(0, m_positionBufferHandle);
            commandContext->set_vertex_buffer(1, m_uvBufferHandle);
            commandContext->set_vertex_buffer(2, m_normalBufferHandle);
            add_detail_timing("resource_bind", bindingStartTime, Clock::now());

            if (frameState.useCpuBatching)
            {
                const Clock::time_point drawSetupStartTime = Clock::now();
                commandContext->set_index_buffer(
                    m_indexBufferHandle, RHI::IndexFormat::UInt32);
                add_detail_timing("index_buffer", drawSetupStartTime, Clock::now());

                const Clock::time_point drawLoopStartTime = Clock::now();
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
                add_detail_timing("cpu_draw_loop", drawLoopStartTime, Clock::now());
            }
            else if (m_indexCountPerInstance > 0 && frameState.objectCount > 0)
            {
                const Clock::time_point drawSetupStartTime = Clock::now();
                commandContext->set_index_buffer(
                    m_indexBufferHandle, RHI::IndexFormat::UInt32);
                add_detail_timing("index_buffer", drawSetupStartTime, Clock::now());

                const Clock::time_point indirectDrawStartTime = Clock::now();
                commandContext->execute_indexed_indirect(
                    m_indirectCommandBufferHandle,
                    m_indirectCommandCountBufferHandle,
                    frameState.objectCount);
                add_detail_timing("execute_indirect", indirectDrawStartTime, Clock::now());
            }

        }

    private:
        static constexpr Math::float4 k_clearColorVec = Math::float4::from_rgba8(63, 63, 63, 255);

        std::string m_name{};
        std::string m_colorName{};
        std::string m_colorRtvName{};
        std::string m_depthName{};
        std::string m_depthDsvName{};
        const DrawFrameState& m_drawFrameState;
        RHI::TextureHandle m_colorHandle{};
        RHI::TextureHandle m_depthHandle{};
        RHI::ViewHandle m_colorRtvHandle{};
        RHI::ViewHandle m_depthDsvHandle{};
        RHI::BufferHandle m_renderObjectBufferHandle{};
        RHI::BufferHandle m_sortedRenderObjectBufferHandle{};
        RHI::BufferHandle m_transformBufferHandle{};
        RHI::BufferHandle m_viewProjectionBufferHandle{};
        RHI::BufferHandle m_positionBufferHandle{};
        RHI::BufferHandle m_uvBufferHandle{};
        RHI::BufferHandle m_normalBufferHandle{};
        RHI::BufferHandle m_indexBufferHandle{};
        RHI::BufferHandle m_meshRangeBufferHandle{};
        RHI::BufferHandle m_materialBufferHandle{};
        LightingSystem::LightingBindings m_lightingBindings{};
        ShadowSystem::ShadowBindings m_shadowBindings{};
        uint32_t m_indexCountPerInstance = 0;
        RHI::TextureHandle m_spotShadowMapHandle{};
        RHI::TextureHandle m_directionalShadowMapHandle{};
        RHI::TextureHandle m_pointShadowMapHandle{};
        RHI::ViewHandle m_spotShadowMapSrvHandle{};
        RHI::ViewHandle m_directionalShadowMapSrvHandle{};
        RHI::ViewHandle m_pointShadowMapSrvHandle{};
        RHI::BufferHandle m_indirectCommandBufferHandle{};
        RHI::BufferHandle m_indirectCommandCountBufferHandle{};
        RHI::BufferHandle m_visibleObjectCountBufferHandle{};
        const RHI::ViewHandle& m_reflectionSkyboxSrvHandle;
        const DebugViewShadingMode* m_shadingMode = nullptr;
        RHI::RootSignatureHandle m_rootSignatureHandle{};
        RHI::ShaderBlobHandle m_vertexShaderHandle{};
        RHI::ShaderBlobHandle m_pixelShaderHandle{};
        RHI::PipelineStateHandle m_pipelineHandle{};
    };
} // namespace Cue::DrawSystem
