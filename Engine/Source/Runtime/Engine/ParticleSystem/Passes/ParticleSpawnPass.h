#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <ParticleSystem/ParticleFrameState.h>

namespace Cue::ParticleSystem
{
    class ParticleSpawnPass final : public RHI::FrameGraphPass
    {
    public:
        ParticleSpawnPass(const ParticleFrameState& a_frameState,
            RHI::BufferHandle a_frameBufferHandle,
            RHI::BufferHandle a_emitterBufferHandle,
            RHI::BufferHandle a_particleBufferHandle)
            : m_frameState(a_frameState)
            , m_frameBufferHandle(a_frameBufferHandle)
            , m_emitterBufferHandle(a_emitterBufferHandle)
            , m_particleBufferHandle(a_particleBufferHandle)
        {}

        const char* name() const noexcept override { return "ParticleSpawn"; }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Compute;
        }

        bool is_enabled(uint32_t a_frameIndex) const noexcept override
        {
            return a_frameIndex < m_frameState.frameStates.size() &&
                m_frameState.frame_state(a_frameIndex).frame.emitterCount > 0;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.read_buffer(m_frameBufferHandle);
            if (!result)
            {
                return result;
            }

            result = builder.read_buffer(m_emitterBufferHandle);
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
            rootSignatureDesc.name = "ParticleSpawnRootSignature";
            rootSignatureDesc.parameters.push_back({
                RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back({
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back({
                RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 0 });
            result = builder.create_root_signature(
                rootSignatureDesc, m_rootSignatureHandle);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc shaderDesc{};
            shaderDesc.name = "ParticleSpawnCS";
            shaderDesc.filePath = "Shaders/D3D12/ParticleSpawn.hlsl";
            shaderDesc.entryPoint = "cs_main";
            shaderDesc.targetProfile = "cs_6_0";
            result = builder.create_shader_blob(shaderDesc, m_computeShaderHandle);
            if (!result)
            {
                return result;
            }

            RHI::ComputePipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "ParticleSpawnPipeline";
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

            result = builder.use_buffer(
                m_emitterBufferHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }

            return builder.use_buffer(
                m_particleBufferHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::UnorderedAccess,
                RHI::ResourceState::UnorderedAccess);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            const ParticleFrameData& frameState =
                m_frameState.frame_state(context.frame_index());
            if (frameState.frame.emitterCount == 0)
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
            commandContext->set_srv(1, m_emitterBufferHandle);
            commandContext->set_uav(2, m_particleBufferHandle);
            commandContext->dispatch(frameState.frame.emitterCount, 1, 1);
        }

    private:
        const ParticleFrameState& m_frameState;
        RHI::BufferHandle m_frameBufferHandle{};
        RHI::BufferHandle m_emitterBufferHandle{};
        RHI::BufferHandle m_particleBufferHandle{};
        RHI::RootSignatureHandle m_rootSignatureHandle{};
        RHI::ShaderBlobHandle m_computeShaderHandle{};
        RHI::PipelineStateHandle m_pipelineHandle{};
    };
} // namespace Cue::ParticleSystem
