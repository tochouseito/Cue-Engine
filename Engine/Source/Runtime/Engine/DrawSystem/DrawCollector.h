// DrawCollector の役割と公開要素を定義する

#pragma once

// === DrawSystem includes ===
#include "DrawScene.h"

namespace Cue::DrawSystem
{
    class DrawCollector final
    {
    public:
        DrawCollector(DrawScene& a_scene, uint32_t a_bufferIndex) noexcept
            : m_scene(a_scene)
            , m_bufferIndex(a_bufferIndex)
        {
        }

        void submit_static_mesh(const StaticMeshDrawItem& a_item)
        {
            m_scene.submit_static_mesh(m_bufferIndex, a_item);
        }

        void submit_sprite(const SpriteDrawItem& a_item)
        {
            m_scene.submit_sprite(m_bufferIndex, a_item);
        }

        void submit_camera(const CameraDrawItem& a_item)
        {
            m_scene.submit_camera(m_bufferIndex, a_item);
        }

    private:
        DrawScene& m_scene;
        uint32_t m_bufferIndex = 0;
    };
}
