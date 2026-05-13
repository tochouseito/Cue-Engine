#pragma once

// === Engine includes ===
#include <ShadowSystem/ShadowScene.h>

namespace Cue::ShadowSystem
{
    class ShadowCollector final
    {
    public:
        ShadowCollector(ShadowScene& a_scene, const uint32_t a_bufferIndex)
            : m_scene(a_scene)
            , m_bufferIndex(a_bufferIndex)
        {}

        void submit_spot_shadow(const SpotShadowItem& a_item)
        {
            m_scene.submit_spot_shadow(m_bufferIndex, a_item);
        }

        void submit_directional_shadow(const DirectionalShadowItem& a_item)
        {
            m_scene.submit_directional_shadow(m_bufferIndex, a_item);
        }

        void submit_point_shadow(const PointShadowItem& a_item)
        {
            m_scene.submit_point_shadow(m_bufferIndex, a_item);
        }

    private:
        ShadowScene& m_scene;
        uint32_t m_bufferIndex = 0;
    };
}
