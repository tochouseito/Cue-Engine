// ShadowScene の役割と公開要素を定義する

#pragma once

// === Engine includes ===
#include <ShadowSystem/GpuData/ShadowData.h>

// === C++ includes ===
#include <array>
#include <vector>

namespace Cue::ShadowSystem
{
    struct SpotShadowItem final
    {
        GpuData::SpotShadowFrameGpu shadow{};
    };

    struct DirectionalShadowItem final
    {
        GpuData::DirectionalShadowFrameGpu shadow{};
    };

    struct PointShadowItem final
    {
        std::array<GpuData::PointShadowFaceGpu,
            GpuData::k_pointShadowFaceCount> faces{};
    };

    struct ShadowSceneFrame final
    {
        DirectionalShadowItem directionalShadow{};
        PointShadowItem pointShadow{};
        std::vector<SpotShadowItem> spotShadows{};
        bool hasDirectionalShadow = false;
        bool hasPointShadow = false;
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

        void submit_directional_shadow(
            const uint32_t a_bufferIndex,
            const DirectionalShadowItem& a_item)
        {
            if (a_bufferIndex >= m_frames.size() ||
                m_frames[a_bufferIndex].hasDirectionalShadow)
            {
                return;
            }

            m_frames[a_bufferIndex].directionalShadow = a_item;
            m_frames[a_bufferIndex].hasDirectionalShadow = true;
        }

        void submit_point_shadow(
            const uint32_t a_bufferIndex,
            const PointShadowItem& a_item)
        {
            if (a_bufferIndex >= m_frames.size() ||
                m_frames[a_bufferIndex].hasPointShadow)
            {
                return;
            }

            m_frames[a_bufferIndex].pointShadow = a_item;
            m_frames[a_bufferIndex].hasPointShadow = true;
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
