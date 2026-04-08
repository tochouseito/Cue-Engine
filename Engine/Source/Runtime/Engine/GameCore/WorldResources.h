#pragma once

// === RHI includes ===
#include <RHI.h>

// === C++ includes ===
#include <array>
#include <vector>

namespace Cue
{
    enum class WorldResourceType : uint32_t
    {
        ObjectInfoBuffer = 0,
        TransformBuffer,
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
        Result create_object_info_buffer(const uint32_t a_maxObjectCount);
        Result create_transform_buffer(const uint32_t a_maxObjectCount);
        Result create_render_object_buffer(const uint32_t a_maxObjectCount);
        Result create_object_count_buffer();

        std::vector<RHI::SlotUploader<GpuData::ObjectInfo>>& object_info_uploaders() noexcept
        {
            return m_objectInfoUploaders;
        }
        std::vector<RHI::SlotUploader<GpuData::ObjectTransformGpu>>& transform_uploaders() noexcept
        {
            return m_transformUploaders;
        }
    private:
        RHI::IBufferManager* m_bufferManager = nullptr;
        RHI::IViewManager* m_viewManager = nullptr;

        std::array<RHI::BufferHandle, static_cast<size_t>(WorldResourceType::Count)> m_bufferHandles{};
        std::array<RHI::ViewHandle, static_cast<size_t>(WorldResourceType::Count)> m_viewHandles{};
        std::vector<RHI::SlotUploader<GpuData::ObjectInfo>> m_objectInfoUploaders{};
        std::vector<RHI::SlotUploader<GpuData::ObjectTransformGpu>> m_transformUploaders{};
    };
}
