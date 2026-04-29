#pragma once

// === RHI includes ===
#include <RHI.h>

// === Engine includes ===
#include <GpuData/Batching.h>
#include <GpuData/Sprite.h>
#include <GpuData/Transform.h>
#include <GpuData/ViewProjection.h>

// === C++ includes ===
#include <array>
#include <vector>

namespace Cue
{
    enum class WorldResourceType : uint32_t
    {
        RenderableInfoBuffer = 0,
        TransformBuffer,
        ViewProjectionBuffer,
        MaterialBuffer,
        RenderObjectBuffer,
        VisibleObjectCountBuffer,
        SpriteInstanceBuffer,
        Count
    };

    class WorldResources final
    {
    public:
        WorldResources(RHI::IBufferManager* bufferManager,
            RHI::IViewManager* viewManager,
            uint32_t a_bufferCount)
            : m_bufferManager(bufferManager)
            , m_viewManager(viewManager)
            , m_bufferCount(a_bufferCount)
        {}
        ~WorldResources() = default;
        WorldResources(const WorldResources&) = delete;
        WorldResources& operator=(const WorldResources&) = delete;
        WorldResources(WorldResources&&) = default;
        WorldResources& operator=(WorldResources&&) = default;

        // ワールド全体で共有されるリソース
        Result create_renderable_info_buffer(const uint32_t a_maxObjectCount);
        Result create_transform_buffer(const uint32_t a_maxObjectCount);
        Result create_view_projection_buffer();
        Result create_material_buffer(const uint32_t a_maxMaterialCount);
        Result create_render_object_buffer(const uint32_t a_maxObjectCount);
        Result create_object_count_buffer();
        Result create_sprite_instance_buffer(const uint32_t a_maxSpriteCount);

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
        std::vector<RHI::SlotUploader<GpuData::SpriteInstanceGpu>>&
            sprite_instance_uploaders() noexcept
        {
            return m_spriteInstanceUploaders;
        }

        [[nodiscard]] RHI::BufferHandle renderable_info_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                WorldResourceType::RenderableInfoBuffer)];
        }

        [[nodiscard]] RHI::BufferHandle transform_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                WorldResourceType::TransformBuffer)];
        }

        [[nodiscard]] RHI::BufferHandle view_projection_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                WorldResourceType::ViewProjectionBuffer)];
        }

        [[nodiscard]] RHI::BufferHandle material_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                WorldResourceType::MaterialBuffer)];
        }

        [[nodiscard]] RHI::BufferHandle render_object_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                WorldResourceType::RenderObjectBuffer)];
        }

        [[nodiscard]] RHI::BufferHandle visible_object_count_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                WorldResourceType::VisibleObjectCountBuffer)];
        }

        [[nodiscard]] RHI::BufferHandle sprite_instance_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                WorldResourceType::SpriteInstanceBuffer)];
        }

        [[nodiscard]] RHI::ViewHandle renderable_info_buffer_srv_handle() const noexcept
        {
            return m_viewHandles[static_cast<size_t>(
                WorldResourceType::RenderableInfoBuffer)];
        }

        [[nodiscard]] RHI::ViewHandle transform_buffer_srv_handle() const noexcept
        {
            return m_viewHandles[static_cast<size_t>(
                WorldResourceType::TransformBuffer)];
        }

        [[nodiscard]] RHI::ViewHandle material_buffer_srv_handle() const noexcept
        {
            return m_viewHandles[static_cast<size_t>(
                WorldResourceType::MaterialBuffer)];
        }

        [[nodiscard]] RHI::ViewHandle render_object_buffer_uav_handle() const noexcept
        {
            return m_viewHandles[static_cast<size_t>(
                WorldResourceType::RenderObjectBuffer)];
        }

        [[nodiscard]] RHI::ViewHandle visible_object_count_buffer_uav_handle() const noexcept
        {
            return m_viewHandles[static_cast<size_t>(
                WorldResourceType::VisibleObjectCountBuffer)];
        }

        [[nodiscard]] RHI::ViewHandle sprite_instance_buffer_srv_handle() const noexcept
        {
            return m_viewHandles[static_cast<size_t>(
                WorldResourceType::SpriteInstanceBuffer)];
        }
    private:
        RHI::IBufferManager* m_bufferManager = nullptr;
        RHI::IViewManager* m_viewManager = nullptr;
        uint32_t m_bufferCount = 1;

        std::array<RHI::BufferHandle, static_cast<size_t>(WorldResourceType::Count)> m_bufferHandles{};
        std::array<RHI::ViewHandle, static_cast<size_t>(WorldResourceType::Count)> m_viewHandles{};
        std::vector<RHI::SlotUploader<GpuData::RenderableInfo>>
            m_renderableInfoUploaders{};
        std::vector<RHI::SlotUploader<GpuData::ObjectTransformGpu>> m_transformUploaders{};
        std::vector<RHI::SlotUploader<GpuData::MaterialGpu>> m_materialUploaders{};
        std::vector<RHI::SlotUploader<GpuData::RenderObject>> m_renderObjectUploaders{};
        std::vector<RHI::SlotUploader<uint32_t>> m_visibleObjectCountUploaders{};
        std::vector<RHI::SlotUploader<GpuData::ViewProjectionGpu>> m_viewProjectionUploaders{};
        std::vector<RHI::SlotUploader<GpuData::SpriteInstanceGpu>>
            m_spriteInstanceUploaders{};
    };
}
