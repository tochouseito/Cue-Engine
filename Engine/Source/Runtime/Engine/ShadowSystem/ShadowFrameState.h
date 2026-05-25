#pragma once

// === Engine includes ===
#include <ShadowSystem/GpuData/ShadowData.h>

// === C++ includes ===
#include <array>
#include <vector>

namespace Cue::ShadowSystem
{
    struct ShadowFrameData final
    {
        GpuData::DirectionalShadowFrameGpu directionalShadow{};
        std::array<GpuData::PointShadowFaceGpu,
            GpuData::k_pointShadowFaceCount> pointShadowFaces{};
        std::array<GpuData::SpotShadowFrameGpu,
            GpuData::k_maxSpotShadowCount> spotShadows{};
    };

    struct ShadowFrameState final
    {
        void resize(const uint32_t a_bufferCount)
        {
            frameStates.resize(a_bufferCount);
        }

        ShadowFrameData& frame_state(const uint32_t a_bufferIndex) noexcept
        {
            return frameStates[a_bufferIndex];
        }

        const ShadowFrameData& frame_state(
            const uint32_t a_bufferIndex) const noexcept
        {
            return frameStates[a_bufferIndex];
        }

        std::vector<ShadowFrameData> frameStates{};
    };
}
