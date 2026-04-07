#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <GameCore/RenderSceneState.h>

namespace Cue
{
    class StaticMeshBatchingPass final : public RHI::FrameGraphPass
    {
    public:
        explicit StaticMeshBatchingPass(const RenderFrameState& a_frameState)
            : m_frameState(a_frameState)
        {}

        const char* name() const noexcept override { return "StaticMeshBatching"; }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Compute;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            Result result =
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
            result = builder.get_buffer("IndirectCommandBuffer",
                m_indirectCommandBufferHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("IndirectCommandCountBuffer",
                m_indirectCommandCountBufferHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_view("IndirectCommandCountBufferUAV",
                m_indirectCommandCountBufferUavHandle);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "StaticMeshBatchingRootSignature";
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 1 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 2 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 3 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 1 });
            result =
                builder.create_root_signature(rootSignatureDesc, m_rootSignatureHandle);
            if (!result)
            {
                return Result::fail(
                    result.code, Severity::Error,
                    "Failed to create root signature for StaticMeshBatching pass.");
            }

            RHI::ShaderCompileDesc computeShaderDesc{};
            computeShaderDesc.name = "StaticMeshBatchingCS";
            computeShaderDesc.filePath = "Shaders/D3D12/StaticMeshBatching.hlsl";
            computeShaderDesc.entryPoint = "CSMain";
            computeShaderDesc.targetProfile = "cs_6_0";
            result =
                builder.create_shader_blob(computeShaderDesc, m_computeShaderHandle);
            if (!result)
            {
                return Result::fail(
                    result.code, Severity::Error,
                    "Failed to compile StaticMeshBatching compute shader.");
            }

            RHI::ComputePipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "StaticMeshBatchingPipeline";
            pipelineDesc.rootSignatureHandle = m_rootSignatureHandle;
            pipelineDesc.csHandle = m_computeShaderHandle;
            result = builder.create_compute_pipeline(pipelineDesc, m_pipelineHandle);
            if (!result)
            {
                return Result::fail(
                    result.code, Severity::Error,
                    "Failed to create compute pipeline for StaticMeshBatching pass.");
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

            const uint32_t clearValues[4] = { 0, 0, 0, 0 };

            {
                RHI::ResourceBarrierDesc barrierDesc{};
                barrierDesc.after = RHI::ResourceState::ShaderResource;
                commandContext->resource_barrier(m_renderObjectBufferHandle, barrierDesc);
            }
            {
                RHI::ResourceBarrierDesc barrierDesc{};
                barrierDesc.after = RHI::ResourceState::ShaderResource;
                commandContext->resource_barrier(m_transformBufferHandle, barrierDesc);
            }
            {
                RHI::ResourceBarrierDesc barrierDesc{};
                barrierDesc.after = RHI::ResourceState::ShaderResource;
                commandContext->resource_barrier(m_meshRangeBufferHandle, barrierDesc);
            }
            {
                RHI::ResourceBarrierDesc barrierDesc{};
                barrierDesc.after = RHI::ResourceState::ShaderResource;
                commandContext->resource_barrier(m_visibleObjectCountBufferHandle,
                    barrierDesc);
            }
            {
                RHI::ResourceBarrierDesc barrierDesc{};
                barrierDesc.after = RHI::ResourceState::UnorderedAccess;
                commandContext->resource_barrier(m_indirectCommandBufferHandle,
                    barrierDesc);
            }
            {
                RHI::ResourceBarrierDesc barrierDesc{};
                barrierDesc.after = RHI::ResourceState::UnorderedAccess;
                commandContext->resource_barrier(m_indirectCommandCountBufferHandle,
                    barrierDesc);
            }

            commandContext->clear_unordered_access_uint(
                m_indirectCommandCountBufferUavHandle, clearValues);
            if (m_frameState.objectCount == 0)
            {
                return;
            }

            commandContext->set_compute_pipeline(m_pipelineHandle);
            commandContext->set_srv(0, m_renderObjectBufferHandle);
            commandContext->set_srv(1, m_transformBufferHandle);
            commandContext->set_srv(2, m_meshRangeBufferHandle);
            commandContext->set_srv(3, m_visibleObjectCountBufferHandle);
            commandContext->set_uav(4, m_indirectCommandBufferHandle);
            commandContext->set_uav(5, m_indirectCommandCountBufferHandle);

            const uint32_t groupCountX = (m_frameState.objectCount + 63u) / 64u;
            commandContext->dispatch(groupCountX, 1, 1);
        }

    private:
        const RenderFrameState& m_frameState;
        RHI::bufferHandle m_renderObjectBufferHandle{};
        RHI::bufferHandle m_transformBufferHandle{};
        RHI::bufferHandle m_meshRangeBufferHandle{};
        RHI::bufferHandle m_visibleObjectCountBufferHandle{};
        RHI::bufferHandle m_indirectCommandBufferHandle{};
        RHI::bufferHandle m_indirectCommandCountBufferHandle{};
        RHI::viewHandle m_indirectCommandCountBufferUavHandle{};
        RHI::rootSignatureHandle m_rootSignatureHandle{};
        RHI::shaderBlobHandle m_computeShaderHandle{};
        RHI::pipelineStateHandle m_pipelineHandle{};
    };
} // namespace Cue
