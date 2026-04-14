#pragma once

// === RHI includes ===
#include <RHI.h>

// === Engine includes ===
#include <GpuData/Batching.h>
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
        RenderObjectBuffer,
        VisibleObjectCountBuffer,
        Count
    };

    class WorldResources final
    {
    public:
        WorldResources(RHI::IBufferManager* bufferManager, RHI::IViewManager* viewManager)
            : m_bufferManager(bufferManager), m_viewManager(viewManager) {}
        ~WorldResources() = default;
        WorldResources(const WorldResources&) = delete;
        WorldResources& operator=(const WorldResources&) = delete;
        WorldResources(WorldResources&&) = default;
        WorldResources& operator=(WorldResources&&) = default;

        // ワールド全体で共有されるリソース
        Result create_renderable_info_buffer(const uint32_t a_maxObjectCount);
        Result create_transform_buffer(const uint32_t a_maxObjectCount);
        Result create_view_projection_buffer();
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
        std::vector<RHI::SlotUploader<GpuData::ViewProjectionGpu>>&
            view_projection_uploaders() noexcept
        {
            return m_viewProjectionUploaders;
        }
    private:
        RHI::IBufferManager* m_bufferManager = nullptr;
        RHI::IViewManager* m_viewManager = nullptr;

        std::array<RHI::BufferHandle, static_cast<size_t>(WorldResourceType::Count)> m_bufferHandles{};
        std::array<RHI::ViewHandle, static_cast<size_t>(WorldResourceType::Count)> m_viewHandles{};
        std::vector<RHI::SlotUploader<GpuData::RenderableInfo>>
            m_renderableInfoUploaders{};
        std::vector<RHI::SlotUploader<GpuData::ObjectTransformGpu>> m_transformUploaders{};
        std::vector<RHI::SlotUploader<GpuData::ViewProjectionGpu>> m_viewProjectionUploaders{};
    };
}
