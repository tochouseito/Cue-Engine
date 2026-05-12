#pragma once

// === Engine includes ===
#include <LightingSystem/GpuData/LightData.h>

// === C++ includes ===
#include <vector>

namespace Cue::LightingSystem
{
    struct DirectionalLightItem final
    {
        GpuData::DirectionalLightGpu light{};
    };

    struct PointLightItem final
    {
        GpuData::PointLightGpu light{};
    };

    struct SpotLightItem final
    {
        GpuData::SpotLightGpu light{};
    };

    struct LightSceneFrame final
    {
        std::vector<DirectionalLightItem> directionalLights{};
        std::vector<PointLightItem> pointLights{};
        std::vector<SpotLightItem> spotLights{};
    };

    class LightScene final
    {
    public:
        void resize(uint32_t a_bufferCount)
        {
            m_frames.resize(a_bufferCount);
        }

        void begin_frame(uint32_t a_bufferIndex)
        {
            if (a_bufferIndex >= m_frames.size())
            {
                return;
            }

            LightSceneFrame& frame = m_frames[a_bufferIndex];
            frame.directionalLights.clear();
            frame.pointLights.clear();
            frame.spotLights.clear();
        }

        void submit_directional(
            uint32_t a_bufferIndex,
            const DirectionalLightItem& a_item)
        {
            if (a_bufferIndex >= m_frames.size())
            {
                return;
            }

            m_frames[a_bufferIndex].directionalLights.push_back(a_item);
        }

        void submit_point(uint32_t a_bufferIndex, const PointLightItem& a_item)
        {
            if (a_bufferIndex >= m_frames.size())
            {
                return;
            }

            m_frames[a_bufferIndex].pointLights.push_back(a_item);
        }

        void submit_spot(uint32_t a_bufferIndex, const SpotLightItem& a_item)
        {
            if (a_bufferIndex >= m_frames.size())
            {
                return;
            }

            m_frames[a_bufferIndex].spotLights.push_back(a_item);
        }

        [[nodiscard]] LightSceneFrame& frame(uint32_t a_bufferIndex) noexcept
        {
            return m_frames[a_bufferIndex];
        }

        [[nodiscard]] const LightSceneFrame& frame(
            uint32_t a_bufferIndex) const noexcept
        {
            return m_frames[a_bufferIndex];
        }

    private:
        std::vector<LightSceneFrame> m_frames{};
    };
}
