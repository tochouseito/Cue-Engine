// DrawScene の役割と公開要素を定義する

#pragma once

// === DrawSystem includes ===
#include "DrawFrameState.h"

// === Engine includes ===
#include <GpuData/Batching.h>
#include <GpuData/Sprite.h>
#include <GpuData/Transform.h>
#include <GpuData/ViewProjection.h>

// === C++ includes ===
#include <vector>

namespace Cue::DrawSystem
{
    enum class StaticMeshRenderQueue : uint8_t
    {
        Opaque,
        Transparent,
    };

    struct StaticMeshVisibilityItem final
    {
        GpuData::RenderableInfo renderableInfo{};
        GpuData::RenderObject renderObject{};
    };

    struct StaticMeshSurfaceItem final
    {
        GpuData::ObjectTransformGpu transform{};
        GpuData::MaterialGpu material{};
        std::vector<GpuData::SkinPaletteGpu> skinPalette{};
        StaticMeshRenderQueue renderQueue = StaticMeshRenderQueue::Opaque;
        bool hasMaterial = false;
    };

    struct StaticMeshBatchItem final
    {
        CpuIndexedDraw cpuIndexedDraw{};
        bool hasCpuIndexedDraw = false;
    };

    struct StaticMeshDrawItem final
    {
        StaticMeshVisibilityItem visibility{};
        StaticMeshSurfaceItem surface{};
        StaticMeshBatchItem batching{};
    };

    struct SpriteDrawItem final
    {
        GpuData::SpriteInstanceGpu instance{};
        int32_t layer = 0;
        uint32_t order = 0;
        uint32_t entity = 0;
    };

    struct CameraDrawItem final
    {
        GpuData::ViewProjectionGpu viewProjection{};
        bool isMain = false;
    };

    struct DrawSceneFrame final
    {
        std::vector<StaticMeshVisibilityItem> staticMeshVisibilityItems{};
        std::vector<StaticMeshSurfaceItem> staticMeshSurfaceItems{};
        std::vector<StaticMeshBatchItem> staticMeshBatchItems{};
        std::vector<SpriteDrawItem> spriteItems{};
        std::vector<CameraDrawItem> cameraItems{};
    };

    class DrawScene final
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

            DrawSceneFrame& frame = m_frames[a_bufferIndex];
            frame.staticMeshVisibilityItems.clear();
            frame.staticMeshSurfaceItems.clear();
            frame.staticMeshBatchItems.clear();
            frame.spriteItems.clear();
            frame.cameraItems.clear();
        }

        [[nodiscard]] uint32_t frame_count() const noexcept
        {
            return static_cast<uint32_t>(m_frames.size());
        }

        void submit_static_mesh(
            uint32_t a_bufferIndex,
            const StaticMeshDrawItem& a_item)
        {
            if (a_bufferIndex >= m_frames.size())
            {
                return;
            }

            DrawSceneFrame& frame = m_frames[a_bufferIndex];
            frame.staticMeshVisibilityItems.push_back(a_item.visibility);
            frame.staticMeshSurfaceItems.push_back(a_item.surface);
            frame.staticMeshBatchItems.push_back(a_item.batching);
        }

        void submit_sprite(
            uint32_t a_bufferIndex,
            const SpriteDrawItem& a_item)
        {
            if (a_bufferIndex >= m_frames.size())
            {
                return;
            }

            m_frames[a_bufferIndex].spriteItems.push_back(a_item);
        }

        void submit_camera(
            uint32_t a_bufferIndex,
            const CameraDrawItem& a_item)
        {
            if (a_bufferIndex >= m_frames.size())
            {
                return;
            }

            m_frames[a_bufferIndex].cameraItems.push_back(a_item);
        }

        [[nodiscard]] DrawSceneFrame& frame(uint32_t a_bufferIndex) noexcept
        {
            return m_frames[a_bufferIndex];
        }

        [[nodiscard]] const DrawSceneFrame& frame(
            uint32_t a_bufferIndex) const noexcept
        {
            return m_frames[a_bufferIndex];
        }

    private:
        std::vector<DrawSceneFrame> m_frames{};
    };
}
