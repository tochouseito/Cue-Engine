// ShadowResources の役割と公開要素を定義する

#pragma once

// === Engine includes ===
#include <ShadowSystem/GpuData/ShadowData.h>
#include <ShadowSystem/ShadowBindings.h>

// === RHI includes ===
#include <BufferManager.h>
#include <SlotUploader.h>
#include <ViewManager.h>

// === C++ includes ===
#include <array>
#include <vector>

namespace Cue::ShadowSystem
{
    enum class ShadowResourceType : uint8_t
    {
        DirectionalShadowFrameBuffer,
        PointShadowFaceBuffer,
        SpotShadowFrameBuffer,
        Count
    };

    class ShadowResources final
    {
    public:
        ShadowResources(
            RHI::IBufferManager* a_bufferManager,
            RHI::IViewManager* a_viewManager,
            uint32_t a_bufferCount);

        [[nodiscard]] Result create_spot_shadow_frame_buffer();
        [[nodiscard]] Result create_directional_shadow_frame_buffer();
        [[nodiscard]] Result create_point_shadow_face_buffer();

        [[nodiscard]] ShadowBindings bindings() const noexcept
        {
            ShadowBindings bindings{};
            bindings.directionalShadowFrameBuffer =
                m_buffers[static_cast<size_t>(
                    ShadowResourceType::DirectionalShadowFrameBuffer)];
            bindings.pointShadowFaceBuffer =
                m_buffers[static_cast<size_t>(
                    ShadowResourceType::PointShadowFaceBuffer)];
            bindings.spotShadowFrameBuffer =
                m_buffers[static_cast<size_t>(
                    ShadowResourceType::SpotShadowFrameBuffer)];
            return bindings;
        }

        [[nodiscard]] std::vector<RHI::SlotUploader<GpuData::SpotShadowFrameGpu>>&
            spot_shadow_frame_uploaders() noexcept
        {
            return m_spotShadowFrameUploaders;
        }

        [[nodiscard]] std::vector<RHI::SlotUploader<GpuData::DirectionalShadowFrameGpu>>&
            directional_shadow_frame_uploaders() noexcept
        {
            return m_directionalShadowFrameUploaders;
        }

        [[nodiscard]] std::vector<RHI::SlotUploader<GpuData::PointShadowFaceGpu>>&
            point_shadow_face_uploaders() noexcept
        {
            return m_pointShadowFaceUploaders;
        }

        [[nodiscard]] RHI::BufferHandle spot_shadow_frame_buffer_handle()
            const noexcept
        {
            return m_buffers[static_cast<size_t>(
                ShadowResourceType::SpotShadowFrameBuffer)];
        }

    private:
        RHI::IBufferManager* m_bufferManager = nullptr;
        RHI::IViewManager* m_viewManager = nullptr;
        uint32_t m_bufferCount = 0;
        std::array<RHI::BufferHandle,
            static_cast<size_t>(ShadowResourceType::Count)> m_buffers{};
        std::vector<RHI::SlotUploader<GpuData::DirectionalShadowFrameGpu>>
            m_directionalShadowFrameUploaders{};
        std::vector<RHI::SlotUploader<GpuData::PointShadowFaceGpu>>
            m_pointShadowFaceUploaders{};
        std::vector<RHI::SlotUploader<GpuData::SpotShadowFrameGpu>>
            m_spotShadowFrameUploaders{};
    };
}
