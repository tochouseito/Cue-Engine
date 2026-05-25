#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <DrawSystem/DrawFrameState.h>

namespace Cue::DrawSystem
{
    class GenerateVisibleListPass final : public RHI::FrameGraphPass
    {
    public:
        GenerateVisibleListPass(const DrawFrameState& a_drawFrameState,
            RHI::BufferHandle a_renderableInfoBufferHandle,
            RHI::BufferHandle a_renderObjectBufferHandle,
            RHI::BufferHandle a_visibleObjectCountBufferHandle,
            RHI::ViewHandle a_renderableInfoBufferSrvHandle,
            RHI::ViewHandle a_renderObjectBufferUavHandle,
            RHI::ViewHandle a_visibleObjectCountBufferUavHandle)
            : m_drawFrameState(a_drawFrameState)
            , m_renderableInfoBufferHandle(a_renderableInfoBufferHandle)
            , m_renderObjectBufferHandle(a_renderObjectBufferHandle)
            , m_visibleObjectCountBufferHandle(a_visibleObjectCountBufferHandle)
            , m_renderableInfoBufferSrvHandle(a_renderableInfoBufferSrvHandle)
            , m_renderObjectBufferUavHandle(a_renderObjectBufferUavHandle)
            , m_visibleObjectCountBufferUavHandle(
                  a_visibleObjectCountBufferUavHandle)
        {}

        const char* name() const noexcept override { return "GenerateVisibleList"; }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Compute;
        }

        bool is_enabled(uint32_t a_frameIndex) const noexcept override
        {
            if (a_frameIndex >= m_drawFrameState.frameStates.size())
            {
                return false;
            }

            return !m_drawFrameState.frame_state(a_frameIndex).useCpuBatching;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.read_buffer(m_renderableInfoBufferHandle);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_renderObjectBufferHandle);
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

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.use_buffer(
                m_renderableInfoBufferHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }

            result = builder.use_buffer(
                m_renderObjectBufferHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::UnorderedAccess,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }

            return builder.use_buffer(
                m_visibleObjectCountBufferHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::UnorderedAccess,
                RHI::ResourceState::ShaderResource);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            const DrawFrameData& frameState =
                m_drawFrameState.frame_state(context.frame_index());
            if (frameState.useCpuBatching)
            {
                return;
            }

            const uint32_t clearValues[4] = { 0, 0, 0, 0 };

            commandContext->clear_unordered_access_uint(
                m_visibleObjectCountBufferUavHandle, clearValues);
            if (frameState.objectCount == 0)
            {
                return;
            }

            commandContext->set_compute_pipeline(m_pipelineHandle);
            commandContext->set_32bit_constant(0, frameState.objectCount);
            commandContext->set_srv(1, m_renderableInfoBufferHandle);
            commandContext->set_uav(2, m_renderObjectBufferHandle);
            commandContext->set_uav(3, m_visibleObjectCountBufferHandle);

            const uint32_t groupCountX = (frameState.objectCount + 63u) / 64u;
            commandContext->dispatch(groupCountX, 1, 1);
        }

    private:
        const DrawFrameState& m_drawFrameState;
        RHI::BufferHandle m_renderableInfoBufferHandle{};
        RHI::BufferHandle m_renderObjectBufferHandle{};
        RHI::BufferHandle m_visibleObjectCountBufferHandle{};
        RHI::ViewHandle m_renderableInfoBufferSrvHandle{};
        RHI::ViewHandle m_renderObjectBufferUavHandle{};
        RHI::ViewHandle m_visibleObjectCountBufferUavHandle{};
        RHI::RootSignatureHandle m_rootSignatureHandle{};
        RHI::ShaderBlobHandle m_computeShaderHandle{};
        RHI::PipelineStateHandle m_pipelineHandle{};
    };
} // namespace Cue::DrawSystem
