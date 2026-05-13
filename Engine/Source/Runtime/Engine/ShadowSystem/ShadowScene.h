#pragma once

// === Engine includes ===
#include <ShadowSystem/GpuData/ShadowData.h>

// === C++ includes ===
#include <vector>

namespace Cue::ShadowSystem
{
    struct SpotShadowItem final
    {
        GpuData::SpotShadowFrameGpu shadow{};
    };

    struct ShadowSceneFrame final
    {
        std::vector<SpotShadowItem> spotShadows{};
    };

    class ShadowScene final
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

            m_frames[a_bufferIndex] = ShadowSceneFrame{};
        }

        void submit_spot_shadow(
            const uint32_t a_bufferIndex,
            const SpotShadowItem& a_item)
        {
            if (a_bufferIndex >= m_frames.size() ||
                m_frames[a_bufferIndex].spotShadows.size() >=
                    GpuData::k_maxSpotShadowCount)
            {
                return;
            }

            m_frames[a_bufferIndex].spotShadows.push_back(a_item);
        }

        ShadowSceneFrame& frame(const uint32_t a_bufferIndex) noexcept
        {
            return m_frames[a_bufferIndex];
        }

        const ShadowSceneFrame& frame(
            const uint32_t a_bufferIndex) const noexcept
        {
            return m_frames[a_bufferIndex];
        }

    private:
        std::vector<ShadowSceneFrame> m_frames{};
    };
}
