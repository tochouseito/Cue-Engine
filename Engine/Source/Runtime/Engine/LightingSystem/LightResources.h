#pragma once

/// ****************************************************************************
/// Point light GPU resources
/// ****************************************************************************

// === RHI includes ===
#include <RHI.h>

// === Engine includes ===
#include "LightingSystem/GpuData/LightData.h"
#include "LightingSystem/LightingBindings.h"

// === C++ includes ===
#include <array>
#include <vector>

namespace Cue::LightingSystem
{
    enum class LightResourceType : uint32_t
    {
        FrameBuffer = 0,
        PointLightBuffer,
        Count
    };

    class LightResources final
    {
    public:
        LightResources(RHI::IBufferManager* bufferManager,
            RHI::IViewManager* viewManager,
            uint32_t bufferCount)
            : m_bufferManager(bufferManager)
            , m_viewManager(viewManager)
            , m_bufferCount(bufferCount)
        {}

        LightResources(const LightResources&) = delete;
        LightResources& operator=(const LightResources&) = delete;
        LightResources(LightResources&&) = default;
        LightResources& operator=(LightResources&&) = default;

        Result create_frame_buffer();
        Result create_point_light_buffer(uint32_t maxLightCount);

        [[nodiscard]] LightingBindings bindings() const noexcept
        {
            LightingBindings bindings{};
            bindings.frameBuffer = frame_buffer_handle();
            bindings.pointLightBuffer = point_light_buffer_handle();
            return bindings;
        }

        std::vector<RHI::SlotUploader<GpuData::LightFrameGpu>>&
            frame_uploaders() noexcept
        {
            return m_frameUploaders;
        }

        std::vector<RHI::SlotUploader<GpuData::PointLightGpu>>&
            point_light_uploaders() noexcept
        {
            return m_pointLightUploaders;
        }

        [[nodiscard]] RHI::BufferHandle frame_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                LightResourceType::FrameBuffer)];
        }

        [[nodiscard]] RHI::BufferHandle point_light_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                LightResourceType::PointLightBuffer)];
        }

    private:
        RHI::IBufferManager* m_bufferManager = nullptr;
        RHI::IViewManager* m_viewManager = nullptr;
        uint32_t m_bufferCount = 1;
        std::array<RHI::BufferHandle, static_cast<size_t>(LightResourceType::Count)>
            m_bufferHandles{};
        std::vector<RHI::SlotUploader<GpuData::LightFrameGpu>> m_frameUploaders{};
        std::vector<RHI::SlotUploader<GpuData::PointLightGpu>>
            m_pointLightUploaders{};
    };
}
