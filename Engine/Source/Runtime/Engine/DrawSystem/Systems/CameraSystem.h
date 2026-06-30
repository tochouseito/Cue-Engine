#pragma once

/// ****************************************************************************
/// CameraComponent を DrawSystem の RenderView へ変換する System
/// ****************************************************************************

// === Base includes ===
#include <CueAssert.h>

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include "DrawSystem/DrawFrameState.h"
#include "DrawSystem/DrawScene.h"
#include "GameCore/Components.h"

// === C++ includes ===
#include <numbers>
#include <vector>

namespace Cue::ECS
{
    class CameraSystem final : public ECSManager::System<WorldTransformComponent, CameraComponent>
    {
    public:
        /// @brief CameraComponent と WorldTransformComponent から DrawScene 用 camera を収集する。
        CameraSystem(const DrawSystem::DrawFrameState& a_drawFrameState,
                     std::vector<DrawSystem::DrawScene>& a_drawScenes)
            : ECSManager::System<WorldTransformComponent, CameraComponent>(
                  [this](Entity a_entity,
                         WorldTransformComponent& a_transform,
                         CameraComponent& a_camera,
                         const UpdateContext& a_context)
                  {
                      update_component(a_entity, a_transform, a_camera, a_context);
                  })
            , m_drawFrameState(a_drawFrameState)
            , m_drawScenes(a_drawScenes)
        {
        }

        /// @brief 現在 frame の camera component を DrawScene へ反映する。
        void update(const UpdateContext& a_context) override
        {
            ECSManager::System<WorldTransformComponent, CameraComponent>::update(a_context);
        }

    private:
        /// @brief 1 camera entity から RenderView を作り、DrawScene に追加する。
        void update_component(Entity,
                              WorldTransformComponent& a_transform,
                              CameraComponent& a_camera,
                              const UpdateContext& a_context)
        {
            if (a_context.bufferIndex >= m_drawFrameState.frameStates.size())
            {
                return;
            }
            if (a_context.bufferIndex >= m_drawScenes.size())
            {
                return;
            }

            const DrawSystem::DrawFrameData& frameState =
                m_drawFrameState.frame_state(a_context.bufferIndex);
            if (frameState.renderWidth == 0 || frameState.renderHeight == 0)
            {
                return;
            }

            const float renderAspectRatio =
                static_cast<float>(frameState.renderWidth) /
                static_cast<float>(frameState.renderHeight);
            const float aspectRatio =
                a_camera.aspectRatio > 0.0f ? a_camera.aspectRatio : renderAspectRatio;

            // CameraSystem は GameCore の transform 階層を辿らず、同期済み WorldTransform だけを見る。
            const Math::float4x4 worldMatrix = Math::make_affine_matrix(
                a_transform.scale,
                a_transform.rotation,
                a_transform.position);

            DrawSystem::CameraDrawItem drawItem{};
            // DrawSystem へ渡すのは CameraComponent ではなく、描画用に確定した RenderView。
            drawItem.renderView.view = Math::float4x4::inverse(worldMatrix);
            drawItem.renderView.projection = Math::perspective_fov_matrix(
                a_camera.fovY * std::numbers::pi_v<float> / 180.0f,
                aspectRatio,
                a_camera.nearZ,
                a_camera.farZ);
            drawItem.renderView.position = a_transform.position;
            drawItem.renderView.width = frameState.renderWidth;
            drawItem.renderView.height = frameState.renderHeight;
            drawItem.renderView.nearZ = a_camera.nearZ;
            drawItem.renderView.farZ = a_camera.farZ;
            drawItem.isMain = a_camera.isMain;

            const Result result = m_drawScenes[a_context.bufferIndex].add_camera(drawItem);
            CUE_ASSERT_FORMAT(success(result), "Failed to add camera to DrawScene: {}", result.message.data());
        }

        // DrawFrameState は描画解像度の参照元。所有権は GameWorld / Engine 側に残す。
        const DrawSystem::DrawFrameState& m_drawFrameState;
        // CameraSystem は frame resource ごとの DrawScene へ camera item を積むだけ。
        std::vector<DrawSystem::DrawScene>& m_drawScenes;
    };
} // namespace Cue::ECS
