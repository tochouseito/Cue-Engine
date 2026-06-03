#pragma once

/// ****************************************************************************
/// ワールド全体で共有される描画リソースの定義
/// *****************************************************************************

// === RHI includes ===
#include <RHI.h>

// === Engine includes ===
#include "GpuData/Batching.h"
#include "GpuData/Transform.h"
#include "GpuData/ViewProjection.h"

// === C++ includes ===
#include <array>
#include <vector>

namespace Cue::DrawSystem
{
    enum class DrawResourceType : uint32_t
    {
        RenderableInfoBuffer = 0,
        TransformBuffer,
        ViewProjectionBuffer,
        MaterialBuffer,
        RenderCellBuffer,
        RenderObjectBuffer,
        VisibleObjectCountBuffer,
        Count
    };

    class DrawResources final
    {
    public:
        DrawResources(RHI::IBufferManager* bufferManager,
            RHI::IViewManager* viewManager,
            uint32_t a_bufferCount)
            : m_bufferManager(bufferManager)
            , m_viewManager(viewManager)
            , m_bufferCount(a_bufferCount)
        {}
        ~DrawResources() = default;
        DrawResources(const DrawResources&) = delete;
        DrawResources& operator=(const DrawResources&) = delete;
        DrawResources(DrawResources&&) = default;
        DrawResources& operator=(DrawResources&&) = default;

        // ワールド全体で共有されるリソース
        Result create_renderable_info_buffer(const uint32_t a_maxObjectCount);
        Result create_transform_buffer(const uint32_t a_maxObjectCount);
        Result create_view_projection_buffer();
        Result create_material_buffer(const uint32_t a_maxMaterialCount);
        Result create_render_cell_buffer(const uint32_t a_maxCellCount);
        Result create_render_object_buffer(const uint32_t a_maxObjectCount);
        Result create_object_count_buffer();

        std::vector<RHI::SlotUploader<GpuData::RenderableInfo>>&
            renderable_info_uploaders() noexcept
        {
            return m_renderableInfoUploaders;
        }
        std::vector<RHI::SlotUploader<GpuData::ObjectTransformGpu>>& transform_uploaders() noexcept
        {
            return m_transformUploaders;
        }
        std::vector<RHI::SlotUploader<GpuData::RenderObject>>&
            render_object_uploaders() noexcept
        {
            return m_renderObjectUploaders;
        }
        std::vector<RHI::SlotUploader<GpuData::MaterialGpu>>&
            material_uploaders() noexcept
        {
            return m_materialUploaders;
        }
        std::vector<RHI::SlotUploader<GpuData::RenderCellGpu>>&
            render_cell_uploaders() noexcept
        {
            return m_renderCellUploaders;
        }
        std::vector<RHI::SlotUploader<uint32_t>>&
            visible_object_count_uploaders() noexcept
        {
            return m_visibleObjectCountUploaders;
        }
        std::vector<RHI::SlotUploader<GpuData::ViewProjectionGpu>>&
            view_projection_uploaders() noexcept
        {
            return m_viewProjectionUploaders;
        }

        [[nodiscard]] RHI::BufferHandle renderable_info_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                DrawResourceType::RenderableInfoBuffer)];
        }

        [[nodiscard]] RHI::BufferHandle transform_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                DrawResourceType::TransformBuffer)];
        }

        [[nodiscard]] RHI::BufferHandle view_projection_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                DrawResourceType::ViewProjectionBuffer)];
        }

        [[nodiscard]] RHI::BufferHandle material_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                DrawResourceType::MaterialBuffer)];
        }

        [[nodiscard]] RHI::BufferHandle render_cell_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                DrawResourceType::RenderCellBuffer)];
        }

        [[nodiscard]] RHI::BufferHandle render_object_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                DrawResourceType::RenderObjectBuffer)];
        }

        [[nodiscard]] RHI::BufferHandle visible_object_count_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                DrawResourceType::VisibleObjectCountBuffer)];
        }

        [[nodiscard]] RHI::ViewHandle renderable_info_buffer_srv_handle() const noexcept
        {
            return m_viewHandles[static_cast<size_t>(
                DrawResourceType::RenderableInfoBuffer)];
        }

        [[nodiscard]] RHI::ViewHandle transform_buffer_srv_handle() const noexcept
        {
            return m_viewHandles[static_cast<size_t>(
                DrawResourceType::TransformBuffer)];
        }

        [[nodiscard]] RHI::ViewHandle material_buffer_srv_handle() const noexcept
        {
            return m_viewHandles[static_cast<size_t>(
                DrawResourceType::MaterialBuffer)];
        }

        [[nodiscard]] RHI::ViewHandle render_cell_buffer_srv_handle() const noexcept
        {
            return m_viewHandles[static_cast<size_t>(
                DrawResourceType::RenderCellBuffer)];
        }

        [[nodiscard]] RHI::ViewHandle render_object_buffer_uav_handle() const noexcept
        {
            return m_viewHandles[static_cast<size_t>(
                DrawResourceType::RenderObjectBuffer)];
        }

        [[nodiscard]] RHI::ViewHandle visible_object_count_buffer_uav_handle() const noexcept
        {
            return m_viewHandles[static_cast<size_t>(
                DrawResourceType::VisibleObjectCountBuffer)];
        }
    private:
        RHI::IBufferManager* m_bufferManager = nullptr;
        RHI::IViewManager* m_viewManager = nullptr;
        uint32_t m_bufferCount = 1;

        std::array<RHI::BufferHandle, static_cast<size_t>(DrawResourceType::Count)> m_bufferHandles{};
        std::array<RHI::ViewHandle, static_cast<size_t>(DrawResourceType::Count)> m_viewHandles{};
        std::vector<RHI::SlotUploader<GpuData::RenderableInfo>>
            m_renderableInfoUploaders{};
        std::vector<RHI::SlotUploader<GpuData::ObjectTransformGpu>> m_transformUploaders{};
        std::vector<RHI::SlotUploader<GpuData::MaterialGpu>> m_materialUploaders{};
        std::vector<RHI::SlotUploader<GpuData::RenderCellGpu>> m_renderCellUploaders{};
        std::vector<RHI::SlotUploader<GpuData::RenderObject>> m_renderObjectUploaders{};
        std::vector<RHI::SlotUploader<uint32_t>> m_visibleObjectCountUploaders{};
        std::vector<RHI::SlotUploader<GpuData::ViewProjectionGpu>> m_viewProjectionUploaders{};
    };
}
