#pragma once

// === LightingSystem includes ===
#include "LightScene.h"

namespace Cue::LightingSystem
{
    class LightCollector final
    {
    public:
        LightCollector(LightScene& a_scene, uint32_t a_bufferIndex) noexcept
            : m_scene(a_scene)
            , m_bufferIndex(a_bufferIndex)
        {
        }

        void submit_directional(const DirectionalLightItem& a_item)
        {
            m_scene.submit_directional(m_bufferIndex, a_item);
        }

        void submit_point(const PointLightItem& a_item)
        {
            m_scene.submit_point(m_bufferIndex, a_item);
        }

        void submit_spot(const SpotLightItem& a_item)
        {
            m_scene.submit_spot(m_bufferIndex, a_item);
        }

    private:
        LightScene& m_scene;
        uint32_t m_bufferIndex = 0;
    };
}
