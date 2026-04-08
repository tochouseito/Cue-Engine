#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <GameCore/RenderSceneState.h>

namespace Cue
{
    class GenerateVisibleListPass final : public RHI::FrameGraphPass
    {
    public:
        explicit GenerateVisibleListPass(const RenderSceneState& a_renderSceneState)
            : m_renderSceneState(a_renderSceneState)
        {}

        const char* name() const noexcept override { return "GenerateVisibleList"; }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Compute;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            // 必要なバッファをフレームグラフに宣言する。
            Result result =
                builder.get_buffer("ObjectInfoBuffer", m_objectInfoBufferHandle);
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
            result = builder.get_buffer("VisibleObjectCountBuffer",
                m_visibleObjectCountBufferHandle);
            if (!result)
            {
                return result;
            }
            result =
                builder.get_view("ObjectInfoBufferSRV", m_objectInfoBufferSrvHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_view("RenderObjectBufferUAV",
                m_renderObjectBufferUavHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_view("VisibleObjectCountBufferUAV",
                m_visibleObjectCountBufferUavHandle);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "GenerateVisibleListRootSignature";
            rootSignatureDesc.parameters.push_back(
                RHI::RootParameterDesc{ RHI::RootParameterType::_32BitConstants,
                                       RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0 });
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
                    "Failed to create root signature for GenerateVisibleList pass.");
            }

            RHI::ShaderCompileDesc computeShaderDesc{};
            computeShaderDesc.name = "GenerateVisibleObjectListCS";
            computeShaderDesc.filePath = "Shaders/D3D12/GenerateVisibleObjectList.hlsl";
            computeShaderDesc.entryPoint = "CSMain";
            computeShaderDesc.targetProfile = "cs_6_0";
            result =
                builder.create_shader_blob(computeShaderDesc, m_computeShaderHandle);
            if (!result)
            {
                return Result::fail(
                    result.code, Severity::Error,
                    "Failed to compile GenerateVisibleObjectList compute shader.");
            }

            RHI::ComputePipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "GenerateVisibleListPipeline";
            pipelineDesc.rootSignatureHandle = m_rootSignatureHandle;
            pipelineDesc.csHandle = m_computeShaderHandle;
            result = builder.create_compute_pipeline(pipelineDesc, m_pipelineHandle);
            if (!result)
            {
                return Result::fail(
                    result.code, Severity::Error,
                    "Failed to create compute pipeline for GenerateVisibleList pass.");
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

            const RenderFrameState& frameState =
                m_renderSceneState.frame_state(context.frame_index());

            const uint32_t clearValues[4] = { 0, 0, 0, 0 };

            {
                RHI::ResourceBarrierDesc barrierDesc{};
                barrierDesc.after = RHI::ResourceState::ShaderResource;
                commandContext->resource_barrier(m_objectInfoBufferHandle, barrierDesc);
            }
            {
                RHI::ResourceBarrierDesc barrierDesc{};
                barrierDesc.after = RHI::ResourceState::UnorderedAccess;
                commandContext->resource_barrier(m_renderObjectBufferHandle, barrierDesc);
            }
            {
                RHI::ResourceBarrierDesc barrierDesc{};
                barrierDesc.after = RHI::ResourceState::UnorderedAccess;
                commandContext->resource_barrier(m_visibleObjectCountBufferHandle,
                    barrierDesc);
            }

            commandContext->clear_unordered_access_uint(
                m_visibleObjectCountBufferUavHandle, clearValues);
            if (frameState.objectCount == 0)
            {
                RHI::ResourceBarrierDesc barrierDesc{};
                barrierDesc.after = RHI::ResourceState::Common;
                commandContext->resource_barrier(m_objectInfoBufferHandle, barrierDesc);
                commandContext->resource_barrier(m_renderObjectBufferHandle, barrierDesc);
                commandContext->resource_barrier(m_visibleObjectCountBufferHandle,
                    barrierDesc);
                return;
            }

            commandContext->set_compute_pipeline(m_pipelineHandle);
            commandContext->set_32bit_constant(0, frameState.objectCount);
            commandContext->set_srv(1, m_objectInfoBufferHandle);
            commandContext->set_uav(2, m_renderObjectBufferHandle);
            commandContext->set_uav(3, m_visibleObjectCountBufferHandle);

            const uint32_t groupCountX = (frameState.objectCount + 63u) / 64u;
            commandContext->dispatch(groupCountX, 1, 1);

            {
                RHI::ResourceBarrierDesc barrierDesc{};
                barrierDesc.after = RHI::ResourceState::Common;
                commandContext->resource_barrier(m_objectInfoBufferHandle, barrierDesc);
            }
        }

    private:
        const RenderSceneState& m_renderSceneState;
        RHI::BufferHandle m_objectInfoBufferHandle{};
        RHI::BufferHandle m_renderObjectBufferHandle{};
        RHI::BufferHandle m_visibleObjectCountBufferHandle{};
        RHI::ViewHandle m_objectInfoBufferSrvHandle{};
        RHI::ViewHandle m_renderObjectBufferUavHandle{};
        RHI::ViewHandle m_visibleObjectCountBufferUavHandle{};
        RHI::RootSignatureHandle m_rootSignatureHandle{};
        RHI::ShaderBlobHandle m_computeShaderHandle{};
        RHI::PipelineStateHandle m_pipelineHandle{};
    };
} // namespace Cue
