#pragma once

// === Engine includes ===
#include <LightingSystem/GpuData/LightData.h>

// === C++ includes ===
#include <vector>

namespace Cue::LightingSystem
{
    struct LightFrameData final
    {
        GpuData::LightFrameGpu frame{};
    };

    struct LightFrameState final
    {
        void resize(uint32_t a_bufferCount)
        {
            frameStates.resize(a_bufferCount);
        }

        [[nodiscard]] LightFrameData& frame_state(uint32_t a_bufferIndex) noexcept
        {
            return frameStates[a_bufferIndex];
        }

        [[nodiscard]] const LightFrameData& frame_state(
            uint32_t a_bufferIndex) const noexcept
        {
            return frameStates[a_bufferIndex];
        }

        std::vector<LightFrameData> frameStates{};
    };
}
