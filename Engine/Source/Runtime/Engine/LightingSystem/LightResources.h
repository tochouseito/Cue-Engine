// LightResources の役割と公開要素を定義する

#pragma once

// === RHI includes ===
#include <RHI.h>

// === LightingSystem includes ===
#include "LightingBindings.h"
#include <LightingSystem/GpuData/LightData.h>

// === C++ includes ===
#include <array>
#include <vector>

namespace Cue::LightingSystem
{
    enum class LightResourceType : uint32_t
    {
        FrameBuffer = 0,
        DirectionalLightBuffer,
        PointLightBuffer,
        SpotLightBuffer,
        Count
    };

    class LightResources final
    {
    public:
        LightResources(RHI::IBufferManager* a_bufferManager,
            RHI::IViewManager* a_viewManager,
            uint32_t a_bufferCount)
            : m_bufferManager(a_bufferManager)
            , m_viewManager(a_viewManager)
            , m_bufferCount(a_bufferCount)
        {
        }

        LightResources(const LightResources&) = delete;
        LightResources& operator=(const LightResources&) = delete;
        LightResources(LightResources&&) = default;
        LightResources& operator=(LightResources&&) = default;

        Result create_frame_buffer();
        Result create_directional_light_buffer(uint32_t a_maxLightCount);
        Result create_point_light_buffer(uint32_t a_maxLightCount);
        Result create_spot_light_buffer(uint32_t a_maxLightCount);

        [[nodiscard]] LightingBindings bindings() const noexcept
        {
            LightingBindings bindings{};
            bindings.frameBuffer = frame_buffer_handle();
            bindings.directionalLightBuffer = directional_light_buffer_handle();
            bindings.pointLightBuffer = point_light_buffer_handle();
            bindings.spotLightBuffer = spot_light_buffer_handle();
            return bindings;
        }

        std::vector<RHI::SlotUploader<GpuData::LightFrameGpu>>&
            frame_uploaders() noexcept
        {
            return m_frameUploaders;
        }

        std::vector<RHI::SlotUploader<GpuData::DirectionalLightGpu>>&
            directional_light_uploaders() noexcept
        {
            return m_directionalLightUploaders;
        }

        std::vector<RHI::SlotUploader<GpuData::PointLightGpu>>&
            point_light_uploaders() noexcept
        {
            return m_pointLightUploaders;
        }

        std::vector<RHI::SlotUploader<GpuData::SpotLightGpu>>&
            spot_light_uploaders() noexcept
        {
            return m_spotLightUploaders;
        }

        [[nodiscard]] RHI::BufferHandle frame_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                LightResourceType::FrameBuffer)];
        }

        [[nodiscard]] RHI::BufferHandle directional_light_buffer_handle()
            const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                LightResourceType::DirectionalLightBuffer)];
        }

        [[nodiscard]] RHI::BufferHandle point_light_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                LightResourceType::PointLightBuffer)];
        }

        [[nodiscard]] RHI::BufferHandle spot_light_buffer_handle() const noexcept
        {
            return m_bufferHandles[static_cast<size_t>(
                LightResourceType::SpotLightBuffer)];
        }

    private:
        RHI::IBufferManager* m_bufferManager = nullptr;
        RHI::IViewManager* m_viewManager = nullptr;
        uint32_t m_bufferCount = 1;
        std::array<RHI::BufferHandle, static_cast<size_t>(LightResourceType::Count)>
            m_bufferHandles{};
        std::vector<RHI::SlotUploader<GpuData::LightFrameGpu>> m_frameUploaders{};
        std::vector<RHI::SlotUploader<GpuData::DirectionalLightGpu>>
            m_directionalLightUploaders{};
        std::vector<RHI::SlotUploader<GpuData::PointLightGpu>>
            m_pointLightUploaders{};
        std::vector<RHI::SlotUploader<GpuData::SpotLightGpu>>
            m_spotLightUploaders{};
    };
}
