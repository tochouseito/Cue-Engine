#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <GameCore/RenderSceneState.h>

// === C++ includes ===
#include <array>
#include <chrono>

namespace Cue
{
    class StaticMeshForwardPass final : public RHI::FrameGraphPass
    {
    public:
        StaticMeshForwardPass(const RenderSceneState& a_renderSceneState,
            uint32_t a_indexCountPerInstance)
            : m_renderSceneState(a_renderSceneState),
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

            RHI::TextureDesc sceneDepthDesc{};
            sceneDepthDesc.name = "SceneDepth";
            sceneDepthDesc.bufferCount = 1;
            sceneDepthDesc.kind = RHI::TextureKind::DepthStencil;
            sceneDepthDesc.width = builder.width();
            sceneDepthDesc.height = builder.height();
            sceneDepthDesc.format = RHI::ColorFormat::D24_UNorm_S8_UInt;
            sceneDepthDesc.clearDepth = 1.0f;
            sceneDepthDesc.clearStencil = 0;
            result = builder.create_texture(sceneDepthDesc, m_sceneDepthHandle);
            if (!result)
            {
                return result;
            }

            RHI::ViewDesc sceneDepthDsvDesc{};
            sceneDepthDsvDesc.name = "SceneDepthDSV";
            sceneDepthDsvDesc.type = RHI::ViewType::DepthStencil;
            sceneDepthDsvDesc.bufferKind = RHI::BufferKind::Texture;
            sceneDepthDsvDesc.textureHandle = m_sceneDepthHandle;
            sceneDepthDsvDesc.colorFormat = RHI::ColorFormat::D24_UNorm_S8_UInt;
            result = builder.create_view(sceneDepthDsvDesc, m_sceneDepthDsvHandle);
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
            result = builder.get_buffer("ViewProjectionBuffer",
                m_viewProjectionBufferHandle);
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
            result = builder.get_buffer("VisibleObjectCountBuffer",
                m_visibleObjectCountBufferHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("MaterialBuffer", m_materialBufferHandle);
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
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 1 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 2 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 3 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 4 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 5 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 6 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 7 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 8 });
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
            pipelineDesc.depthStencilState.depthEnable = true;
            pipelineDesc.depthStencilState.depthWriteMask = RHI::DepthWriteMask::All;
            pipelineDesc.depthStencilState.depthFunc = RHI::ComparisonFunc::LessEqual;
            pipelineDesc.dsvFormat = RHI::ColorFormat::D24_UNorm_S8_UInt;
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

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.use_texture(
                m_finalColorHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::RenderTarget,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }

            result = builder.use_texture(
                m_sceneDepthHandle,
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
            commandContext->clear_render_target(m_finalColorRtvHandle,
                k_clearColorVec.data());
            commandContext->clear_depth_stencil(m_sceneDepthDsvHandle, 1.0f, 0);
            add_detail_timing("clear", clearStartTime, Clock::now());

            const Clock::time_point targetSetupStartTime = Clock::now();
            commandContext->set_render_targets(
                &m_finalColorRtvHandle,
                1,
                m_sceneDepthDsvHandle);
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
            commandContext->set_srv(2, m_renderObjectBufferHandle);
            commandContext->set_srv(3, m_transformBufferHandle);
            commandContext->set_srv(4, m_positionBufferHandle);
            commandContext->set_srv(5, m_uvBufferHandle);
            commandContext->set_srv(6, m_normalBufferHandle);
            commandContext->set_srv(7, m_indexBufferHandle);
            commandContext->set_srv(8, m_meshRangeBufferHandle);
            commandContext->set_srv(9, m_visibleObjectCountBufferHandle);
            commandContext->set_srv(10, m_materialBufferHandle);
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

        const RenderSceneState& m_renderSceneState;
        uint32_t m_indexCountPerInstance = 0;
        RHI::TextureHandle m_finalColorHandle{};
        RHI::TextureHandle m_sceneDepthHandle{};
        RHI::ViewHandle m_finalColorRtvHandle{};
        RHI::ViewHandle m_sceneDepthDsvHandle{};
        RHI::BufferHandle m_renderObjectBufferHandle{};
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
