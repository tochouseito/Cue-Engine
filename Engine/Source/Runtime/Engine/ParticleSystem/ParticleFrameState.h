#pragma once

// === Engine includes ===
#include <GpuData/Particle.h>

// === C++ includes ===
#include <vector>

namespace Cue::ParticleSystem
{
    struct ParticleFrameData final
    {
        GpuData::ParticleFrameGpu frame{};
    };

    struct ParticleFrameState final
    {
        void resize(const uint32_t a_bufferCount)
        {
            frameStates.resize(a_bufferCount);
        }

        ParticleFrameData& frame_state(const uint32_t a_bufferIndex) noexcept
        {
            return frameStates[a_bufferIndex];
        }

        const ParticleFrameData& frame_state(const uint32_t a_bufferIndex) const noexcept
        {
            return frameStates[a_bufferIndex];
        }

        std::vector<ParticleFrameData> frameStates{};
    };
} // namespace Cue::ParticleSystem
