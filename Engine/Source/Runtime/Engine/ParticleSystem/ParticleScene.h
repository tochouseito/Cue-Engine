#pragma once

// === Engine includes ===
#include <GpuData/Particle.h>

// === C++ includes ===
#include <vector>

namespace Cue::ParticleSystem
{
    struct ParticleEmitterItem final
    {
        GpuData::ParticleEmitterGpu emitter{};
    };

    struct ParticleSceneFrame final
    {
        std::vector<ParticleEmitterItem> emitters{};
    };

    class ParticleScene final
    {
    public:
        void resize(const uint32_t a_bufferCount)
        {
            m_frames.resize(a_bufferCount);
        }

        void begin_frame(const uint32_t a_bufferIndex)
        {
            if (a_bufferIndex >= m_frames.size())
            {
                return;
            }

            m_frames[a_bufferIndex].emitters.clear();
        }

        bool submit_emitter(
            const uint32_t a_bufferIndex,
            const ParticleEmitterItem& a_item)
        {
            if (a_bufferIndex >= m_frames.size())
            {
                return false;
            }

            ParticleSceneFrame& frame = m_frames[a_bufferIndex];
            if (frame.emitters.size() >= GpuData::k_maxParticleEmitterCount)
            {
                return false;
            }

            frame.emitters.push_back(a_item);
            return true;
        }

        ParticleSceneFrame& frame(const uint32_t a_bufferIndex) noexcept
        {
            return m_frames[a_bufferIndex];
        }

        const ParticleSceneFrame& frame(const uint32_t a_bufferIndex) const noexcept
        {
            return m_frames[a_bufferIndex];
        }

    private:
        std::vector<ParticleSceneFrame> m_frames{};
    };
} // namespace Cue::ParticleSystem
