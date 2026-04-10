#pragma once

// === Base includes ===
#include <CueAssert.h>

// === RHI includes ===
#include <RHI.h>

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <GameCore/Components.h>
#include <GpuData/ViewProjection.h>

namespace Cue::ECS
{
    class CameraSystem final
        : public ECSManager::System<TransformComponent, CameraComponent>
    {
    public:
        explicit CameraSystem(std::vector<RHI::SlotUploader<GpuData::ViewProjectionGpu>>& a_viewProjectionUploaders)
            : ECSManager::System<TransformComponent, CameraComponent>(
                [this](Entity a_entity, TransformComponent& a_transform,
                    CameraComponent& a_camera, const UpdateContext& a_context) {
                        update_component(a_entity, a_transform, a_camera, a_context);
                },
                [this](Entity a_entity, TransformComponent& a_transform,
                    CameraComponent& a_camera, const InitializeContext& a_context) {
                        initialize_component(a_entity, a_transform, a_camera, a_context);
                },
                [this](Entity a_entity, TransformComponent& a_transform,
                    CameraComponent& a_camera, const FinalizeContext& a_context) {
                        finalize_component(a_entity, a_transform, a_camera, a_context);
                }),
            m_viewProjectionUploaders(a_viewProjectionUploaders)
        {}

        void update(const UpdateContext& a_context) override
        {
            m_currentUploader = nullptr;
            if (!m_viewProjectionUploaders.empty())
            {
                const uint32_t uploaderIndex =
                    (m_viewProjectionUploaders.size() == 1) ? 0u : a_context.bufferIndex;
                if (uploaderIndex < m_viewProjectionUploaders.size())
                {
                    m_currentUploader = &m_viewProjectionUploaders[uploaderIndex];
                    m_currentUploader->begin_frame();
                }
            }

            ECSManager::System<TransformComponent, CameraComponent>::update(a_context);
            if (m_currentUploader != nullptr && !m_currentUploader->commit())
            {
                CUE_ASSERTF(false, "Failed to commit view projection uploads.");
            }
        }

    private:
        void update_component(Entity a_entity, TransformComponent& a_transform,
            CameraComponent& a_camera, const UpdateContext& a_context)
        {
            a_entity;
            a_context;
            GpuData::ViewProjectionGpu gpuViewProjection{};
            gpuViewProjection.view = Math::make_affine_matrix(
                a_transform.scale,
                a_transform.rotation,
                a_transform.position);
            gpuViewProjection.projection = Math::perspective_fov_matrix(
                a_camera.fovY * Math::k_pi / 180.0f,
                a_camera.aspectRatio,
                a_camera.nearZ,
                a_camera.farZ);

            if (!m_currentUploader->push(a_entity, gpuViewProjection))
            {
                CUE_ASSERTF(false, "Failed to queue view projection upload. entity=%u", a_entity);
            }
        }

        void initialize_component(Entity a_entity, TransformComponent& a_transform,
            CameraComponent& a_camera, const InitializeContext& a_context)
        {
            a_entity;
            a_transform;
            a_camera;
            a_context;
        }

        void finalize_component(Entity a_entity, TransformComponent& a_transform,
            CameraComponent& a_camera, const FinalizeContext& a_context)
        {
            a_entity;
            a_transform;
            a_camera;
            a_context;
        }

    private:
        std::vector<RHI::SlotUploader<GpuData::ViewProjectionGpu>>& m_viewProjectionUploaders;
        RHI::SlotUploader<GpuData::ViewProjectionGpu>* m_currentUploader = nullptr;
    };
} // namespace Cue::ECS
