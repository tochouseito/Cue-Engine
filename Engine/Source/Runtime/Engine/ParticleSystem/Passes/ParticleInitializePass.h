#pragma once

// === RHI includes ===
#include <FrameGraph.h>

namespace Cue::ParticleSystem
{
    class ParticleInitializePass final : public RHI::FrameGraphPass
    {
    public:
        ParticleInitializePass(
            RHI::BufferHandle a_particleBufferHandle,
            uint32_t a_maxParticleCount)
            : m_particleBufferHandle(a_particleBufferHandle)
            , m_maxParticleCount(a_maxParticleCount)
        {}

        const char* name() const noexcept override { return "ParticleInitialize"; }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Compute;
        }

        bool is_enabled(uint32_t a_frameIndex) const noexcept override
        {
            a_frameIndex;
            return !m_hasInitialized;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.read_buffer(m_particleBufferHandle);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "ParticleInitializeRootSignature";
            rootSignatureDesc.parameters.push_back(
                RHI::RootParameterDesc{ RHI::RootParameterType::_32BitConstants,
                                       RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back({
                RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 0 });
            result = builder.create_root_signature(
                rootSignatureDesc, m_rootSignatureHandle);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc shaderDesc{};
            shaderDesc.name = "ParticleInitializeCS";
            shaderDesc.filePath = "Shaders/D3D12/ParticleInitialize.hlsl";
            shaderDesc.entryPoint = "cs_main";
            shaderDesc.targetProfile = "cs_6_0";
            result = builder.create_shader_blob(shaderDesc, m_computeShaderHandle);
            if (!result)
            {
                return result;
            }

            RHI::ComputePipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "ParticleInitializePipeline";
            pipelineDesc.rootSignatureHandle = m_rootSignatureHandle;
            pipelineDesc.csHandle = m_computeShaderHandle;
            return builder.create_compute_pipeline(pipelineDesc, m_pipelineHandle);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            return builder.use_buffer(
                m_particleBufferHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::UnorderedAccess,
                RHI::ResourceState::ShaderResource);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            if (m_hasInitialized || m_maxParticleCount == 0)
            {
                return;
            }

            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            commandContext->set_compute_pipeline(m_pipelineHandle);
            commandContext->set_32bit_constant(0, m_maxParticleCount);
            commandContext->set_uav(1, m_particleBufferHandle);
            commandContext->dispatch((m_maxParticleCount + 63u) / 64u, 1, 1);
            m_hasInitialized = true;
        }

    private:
        RHI::BufferHandle m_particleBufferHandle{};
        uint32_t m_maxParticleCount = 0;
        RHI::RootSignatureHandle m_rootSignatureHandle{};
        RHI::ShaderBlobHandle m_computeShaderHandle{};
        RHI::PipelineStateHandle m_pipelineHandle{};
        mutable bool m_hasInitialized = false;
    };
} // namespace Cue::ParticleSystem
