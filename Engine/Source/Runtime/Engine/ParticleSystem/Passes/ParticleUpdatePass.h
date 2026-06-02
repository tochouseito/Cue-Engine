// ParticleUpdatePass の役割と公開要素を定義する

#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <ParticleSystem/ParticleFrameState.h>

namespace Cue::ParticleSystem
{
    class ParticleUpdatePass final : public RHI::FrameGraphPass
    {
    public:
        ParticleUpdatePass(const ParticleFrameState& a_frameState,
            RHI::BufferHandle a_frameBufferHandle,
            RHI::BufferHandle a_particleBufferHandle)
            : m_frameState(a_frameState)
            , m_frameBufferHandle(a_frameBufferHandle)
            , m_particleBufferHandle(a_particleBufferHandle)
        {}

        const char* name() const noexcept override { return "ParticleUpdate"; }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Compute;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.read_buffer(m_frameBufferHandle);
            if (!result)
            {
                return result;
            }

            result = builder.read_buffer(m_particleBufferHandle);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "ParticleUpdateRootSignature";
            rootSignatureDesc.parameters.push_back({
                RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back({
                RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 0 });
            result = builder.create_root_signature(
                rootSignatureDesc, m_rootSignatureHandle);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc shaderDesc{};
            shaderDesc.name = "ParticleUpdateCS";
            shaderDesc.filePath = "Shaders/D3D12/ParticleUpdate.hlsl";
            shaderDesc.entryPoint = "cs_main";
            shaderDesc.targetProfile = "cs_6_0";
            result = builder.create_shader_blob(shaderDesc, m_computeShaderHandle);
            if (!result)
            {
                return result;
            }

            RHI::ComputePipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "ParticleUpdatePipeline";
            pipelineDesc.rootSignatureHandle = m_rootSignatureHandle;
            pipelineDesc.csHandle = m_computeShaderHandle;
            return builder.create_compute_pipeline(pipelineDesc, m_pipelineHandle);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.use_buffer(
                m_frameBufferHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }

            return builder.use_buffer(
                m_particleBufferHandle,
                RHI::ResourceAccessType::ReadWrite,
                RHI::ResourceState::UnorderedAccess,
                RHI::ResourceState::ShaderResource);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            const ParticleFrameData& frameState =
                m_frameState.frame_state(context.frame_index());
            if (frameState.frame.particleCount == 0)
            {
                return;
            }

            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            commandContext->set_compute_pipeline(m_pipelineHandle);
            commandContext->set_cbv(0, m_frameBufferHandle);
            commandContext->set_uav(1, m_particleBufferHandle);
            commandContext->dispatch(
                (frameState.frame.particleCount + 63u) / 64u,
                1,
                1);
        }

    private:
        const ParticleFrameState& m_frameState;
        RHI::BufferHandle m_frameBufferHandle{};
        RHI::BufferHandle m_particleBufferHandle{};
        RHI::RootSignatureHandle m_rootSignatureHandle{};
        RHI::ShaderBlobHandle m_computeShaderHandle{};
        RHI::PipelineStateHandle m_pipelineHandle{};
    };
} // namespace Cue::ParticleSystem
