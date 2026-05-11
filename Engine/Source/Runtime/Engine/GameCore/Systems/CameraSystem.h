#pragma once

// === Base includes ===
#include <CueAssert.h>

// === RHI includes ===
#include <RHI.h>

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <GameCore/Components.h>
#include <DrawSystem/DrawFrameState.h>
#include <GpuData/ViewProjection.h>

namespace Cue::ECS
{
    class CameraSystem final
        : public ECSManager::System<TransformComponent, CameraComponent>
    {
    public:
        explicit CameraSystem(
            std::vector<RHI::SlotUploader<GpuData::ViewProjectionGpu>>&
                a_viewProjectionUploaders,
            const DrawSystem::DrawFrameState& a_drawFrameState)
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
            m_viewProjectionUploaders(a_viewProjectionUploaders),
            m_drawFrameState(a_drawFrameState)
        {}

        void update(const UpdateContext& a_context) override
        {
            m_currentUploader = nullptr;
            m_hasUploadedCamera = false;
            m_hasMainCamera = false;
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
            if (m_currentUploader == nullptr || !a_camera.is_active())
            {
                return;
            }

            const DrawSystem::DrawFrameData& frameState =
                m_drawFrameState.frame_state(a_context.bufferIndex);
            if (frameState.renderWidth == 0 || frameState.renderHeight == 0)
            {
                return;
            }

            if (!a_camera.isMain && (m_hasMainCamera || m_hasUploadedCamera))
            {
                return;
            }

            GpuData::ViewProjectionGpu gpuViewProjection{};
            Math::float4x4 worldMatrix = Math::make_affine_matrix(
                a_transform.scale,
                a_transform.rotation,
                a_transform.position);
            const float aspectRatio = static_cast<float>(frameState.renderWidth) /
                static_cast<float>(frameState.renderHeight);
            gpuViewProjection.view = Math::float4x4::inverse(worldMatrix);
            gpuViewProjection.projection = Math::perspective_fov_matrix(
                a_camera.fovY * Math::k_pi / 180.0f,
                aspectRatio,
                a_camera.nearZ,
                a_camera.farZ);

            if (!m_currentUploader->push(0, gpuViewProjection))
            {
                CUE_ASSERTF(false, "Failed to queue view projection upload. entity=%u", a_entity);
                return;
            }

            m_hasUploadedCamera = true;
            if (a_camera.isMain)
            {
                m_hasMainCamera = true;
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
        const DrawSystem::DrawFrameState& m_drawFrameState;
        RHI::SlotUploader<GpuData::ViewProjectionGpu>* m_currentUploader = nullptr;
        bool m_hasUploadedCamera = false;
        bool m_hasMainCamera = false;
    };
} // namespace Cue::ECS
