#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <GpuData/Particle.h>
#include <ParticleSystem/ParticleFrameState.h>

namespace Cue::ParticleSystem
{
    class ParticleBufferCopyPass final : public RHI::FrameGraphPass
    {
    public:
        ParticleBufferCopyPass(const ParticleFrameState& a_frameState,
            RHI::BufferHandle a_frameBufferHandle,
            RHI::BufferHandle a_emitterBufferHandle)
            : m_frameState(a_frameState)
            , m_frameBufferHandle(a_frameBufferHandle)
            , m_emitterBufferHandle(a_emitterBufferHandle)
        {}

        const char* name() const noexcept override { return "ParticleBufferCopy"; }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Copy;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.read_buffer(m_frameBufferHandle);
            if (!result)
            {
                return result;
            }

            return builder.read_buffer(m_emitterBufferHandle);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.use_buffer(
                m_frameBufferHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::CopyDest,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }

            return builder.use_buffer(
                m_emitterBufferHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::CopyDest,
                RHI::ResourceState::ShaderResource);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            if (!m_frameBufferHandle.valid() || !m_emitterBufferHandle.valid())
            {
                return;
            }

            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            const ParticleFrameData& frameState =
                m_frameState.frame_state(context.frame_index());

            RHI::BufferCopyRegion frameRegion{};
            frameRegion.srcBufferHandle = m_frameBufferHandle;
            frameRegion.srcUploadResourceIndex = context.frame_index();
            frameRegion.dstBufferHandle = m_frameBufferHandle;
            frameRegion.dstDefaultResourceIndex = 0;
            frameRegion.byteSize = sizeof(GpuData::ParticleFrameGpu);
            (void)commandContext->copy_buffer_region(frameRegion);

            if (frameState.frame.emitterCount == 0)
            {
                return;
            }

            RHI::BufferCopyRegion emitterRegion{};
            emitterRegion.srcBufferHandle = m_emitterBufferHandle;
            emitterRegion.srcUploadResourceIndex = context.frame_index();
            emitterRegion.dstBufferHandle = m_emitterBufferHandle;
            emitterRegion.dstDefaultResourceIndex = 0;
            emitterRegion.byteSize =
                static_cast<uint64_t>(frameState.frame.emitterCount) *
                sizeof(GpuData::ParticleEmitterGpu);
            (void)commandContext->copy_buffer_region(emitterRegion);
        }

    private:
        const ParticleFrameState& m_frameState;
        RHI::BufferHandle m_frameBufferHandle{};
        RHI::BufferHandle m_emitterBufferHandle{};
    };
} // namespace Cue::ParticleSystem
