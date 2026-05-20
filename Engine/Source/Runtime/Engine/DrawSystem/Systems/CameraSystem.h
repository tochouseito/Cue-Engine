#pragma once

// === Base includes ===
#include <CueAssert.h>

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <DrawSystem/DrawCollector.h>
#include <DrawSystem/DrawScene.h>
#include <GameCore/Components.h>
#include <DrawSystem/DrawFrameState.h>
#include <GpuData/ViewProjection.h>

namespace Cue::ECS
{
    class CameraSystem final
        : public ECSManager::System<WorldTransformComponent, CameraComponent>
    {
    public:
        explicit CameraSystem(
            const DrawSystem::DrawFrameState& a_drawFrameState,
            DrawSystem::DrawScene& a_drawScene)
            : ECSManager::System<WorldTransformComponent, CameraComponent>(
                [this](Entity a_entity, WorldTransformComponent& a_transform,
                    CameraComponent& a_camera, const UpdateContext& a_context) {
                        update_component(a_entity, a_transform, a_camera, a_context);
                },
                [this](Entity a_entity, WorldTransformComponent& a_transform,
                    CameraComponent& a_camera, const InitializeContext& a_context) {
                        initialize_component(a_entity, a_transform, a_camera, a_context);
                },
                [this](Entity a_entity, WorldTransformComponent& a_transform,
                    CameraComponent& a_camera, const FinalizeContext& a_context) {
                        finalize_component(a_entity, a_transform, a_camera, a_context);
                }),
            m_drawFrameState(a_drawFrameState),
            m_drawScene(a_drawScene)
        {}

        void update(const UpdateContext& a_context) override
        {
            DrawSystem::DrawCollector collector(m_drawScene, a_context.bufferIndex);
            m_currentCollector = &collector;
            ECSManager::System<WorldTransformComponent, CameraComponent>::update(a_context);
            m_currentCollector = nullptr;
        }

    private:
        void update_component(Entity a_entity, WorldTransformComponent& a_transform,
            CameraComponent& a_camera, const UpdateContext& a_context)
        {
            a_entity;
            if (m_currentCollector == nullptr || !a_camera.is_active())
            {
                return;
            }

            const DrawSystem::DrawFrameData& frameState =
                m_drawFrameState.frame_state(a_context.bufferIndex);
            if (frameState.renderWidth == 0 || frameState.renderHeight == 0)
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

            DrawSystem::CameraDrawItem drawItem{};
            drawItem.viewProjection = gpuViewProjection;
            drawItem.isMain = a_camera.isMain;
            m_currentCollector->submit_camera(drawItem);
        }

        void initialize_component(Entity a_entity, WorldTransformComponent& a_transform,
            CameraComponent& a_camera, const InitializeContext& a_context)
        {
            a_entity;
            a_transform;
            a_camera;
            a_context;
        }

        void finalize_component(Entity a_entity, WorldTransformComponent& a_transform,
            CameraComponent& a_camera, const FinalizeContext& a_context)
        {
            a_entity;
            a_transform;
            a_camera;
            a_context;
        }

    private:
        const DrawSystem::DrawFrameState& m_drawFrameState;
        DrawSystem::DrawScene& m_drawScene;
        DrawSystem::DrawCollector* m_currentCollector = nullptr;
    };
} // namespace Cue::ECS
