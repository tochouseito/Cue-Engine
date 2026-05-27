#pragma once

// === RHI includes ===
#include <RHI.h>

// === Engine includes ===
#include <GpuData/Particle.h>

// === C++ includes ===
#include <array>
#include <vector>

namespace Cue::ParticleSystem
{
    enum class ParticleResourceType : uint32_t
    {
        FrameBuffer = 0,
        EmitterBuffer,
        ParticleBuffer,
        TrailBuffer,
        Count
    };

    class ParticleResources final
    {
    public:
        ParticleResources(RHI::IBufferManager* a_bufferManager,
            RHI::IViewManager* a_viewManager,
            uint32_t a_bufferCount)
            : m_bufferManager(a_bufferManager)
            , m_viewManager(a_viewManager)
            , m_bufferCount(a_bufferCount)
        {}

        Result create_frame_buffer();
        Result create_emitter_buffer(uint32_t a_maxEmitterCount);
        Result create_particle_buffer(uint32_t a_maxParticleCount);
        Result create_trail_buffer(
            uint32_t a_maxParticleCount,
            uint32_t a_maxTrailSegmentCount);

        std::vector<RHI::SlotUploader<GpuData::ParticleFrameGpu>>&
            frame_uploaders() noexcept
        {
            return m_frameUploaders;
        }

        std::vector<RHI::SlotUploader<GpuData::ParticleEmitterGpu>>&
            emitter_uploaders() noexcept
        {
            return m_emitterUploaders;
        }

        [[nodiscard]] RHI::BufferHandle frame_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                ParticleResourceType::FrameBuffer)];
        }

        [[nodiscard]] RHI::BufferHandle emitter_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                ParticleResourceType::EmitterBuffer)];
        }

        [[nodiscard]] RHI::BufferHandle particle_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                ParticleResourceType::ParticleBuffer)];
        }

        [[nodiscard]] RHI::BufferHandle trail_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                ParticleResourceType::TrailBuffer)];
        }

        [[nodiscard]] RHI::ViewHandle particle_buffer_uav_handle() const noexcept
        {
            return m_viewHandles[static_cast<size_t>(
                ParticleResourceType::ParticleBuffer)];
        }

        [[nodiscard]] uint32_t max_particle_count() const noexcept
        {
            return m_maxParticleCount;
        }

    private:
        RHI::IBufferManager* m_bufferManager = nullptr;
        RHI::IViewManager* m_viewManager = nullptr;
        uint32_t m_bufferCount = 1;
        uint32_t m_maxParticleCount = 0;
        uint32_t m_maxTrailSegmentCount = 0;

        std::array<RHI::BufferHandle, static_cast<size_t>(ParticleResourceType::Count)> m_bufferHandles{};
        std::array<RHI::ViewHandle, static_cast<size_t>(ParticleResourceType::Count)> m_viewHandles{};
        std::vector<RHI::SlotUploader<GpuData::ParticleFrameGpu>> m_frameUploaders{};
        std::vector<RHI::SlotUploader<GpuData::ParticleEmitterGpu>> m_emitterUploaders{};
    };
} // namespace Cue::ParticleSystem
