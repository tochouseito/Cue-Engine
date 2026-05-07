#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <GameCore/RenderSceneState.h>

// === C++ includes ===
#include <array>
#include <chrono>
#include <string>
#include <utility>

namespace Cue
{
    class StaticMeshForwardPass final : public RHI::FrameGraphPass
    {
    public:
        StaticMeshForwardPass(std::string a_name,
            std::string a_colorName,
            std::string a_colorRtvName,
            std::string a_depthName,
            std::string a_depthDsvName,
            const RenderSceneState& a_renderSceneState,
            RHI::BufferHandle a_renderObjectBufferHandle,
            RHI::BufferHandle a_transformBufferHandle,
            RHI::BufferHandle a_viewProjectionBufferHandle,
            RHI::BufferHandle a_visibleObjectCountBufferHandle,
            RHI::BufferHandle a_materialBufferHandle,
            uint32_t a_indexCountPerInstance)
            : m_name(std::move(a_name)),
            m_colorName(std::move(a_colorName)),
            m_colorRtvName(std::move(a_colorRtvName)),
            m_depthName(std::move(a_depthName)),
            m_depthDsvName(std::move(a_depthDsvName)),
            m_renderSceneState(a_renderSceneState),
            m_renderObjectBufferHandle(a_renderObjectBufferHandle),
            m_transformBufferHandle(a_transformBufferHandle),
            m_viewProjectionBufferHandle(a_viewProjectionBufferHandle),
            m_visibleObjectCountBufferHandle(a_visibleObjectCountBufferHandle),
            m_materialBufferHandle(a_materialBufferHandle),
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

            const RenderFrameState& frameState =
                m_renderSceneState.frame_state(context.frame_index());

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
        const RenderSceneState& m_renderSceneState;
        uint32_t m_indexCountPerInstance = 0;
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
        RHI::BufferHandle m_indirectCommandBufferHandle{};
        RHI::BufferHandle m_indirectCommandCountBufferHandle{};
        RHI::BufferHandle m_visibleObjectCountBufferHandle{};
        RHI::RootSignatureHandle m_rootSignatureHandle{};
        RHI::ShaderBlobHandle m_vertexShaderHandle{};
        RHI::ShaderBlobHandle m_pixelShaderHandle{};
        RHI::PipelineStateHandle m_pipelineHandle{};
    };
} // namespace Cue
