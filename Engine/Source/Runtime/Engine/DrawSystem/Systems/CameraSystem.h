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
#include "GameCore/GameWorld.h"

// === C++ includes ===
#include <numbers>
#include <vector>

namespace Cue::ECS
{
    class CameraSystem final : public ECSManager::ISystem
    {
    public:
        /// @brief GameWorld が保持する描画 Camera から DrawScene 用 camera を生成する。
        CameraSystem(GameCore::GameWorld& a_world,
                     const DrawSystem::DrawFrameState& a_drawFrameState,
                     std::vector<DrawSystem::DrawScene>& a_drawScenes)
            : m_world(a_world)
            , m_drawFrameState(a_drawFrameState)
            , m_drawScenes(a_drawScenes)
        {
        }

        /// @brief context 無しの呼び出しでは既定 frame として処理する。
        void update() override
        {
            update(UpdateContext{});
        }

        /// @brief 現在 frame の描画 Camera を DrawScene へ反映する。
        void update(const UpdateContext& a_context) override
        {
            if (a_context.bufferIndex >= m_drawFrameState.frameStates.size())
            {
                return;
            }
            if (a_context.bufferIndex >= m_drawScenes.size())
            {
                return;
            }

            const GameCore::EntityId cameraEntity = m_world.render_camera_entity();
            if (cameraEntity == GameCore::k_invalidEntityId)
            {
                return;
            }

            WorldTransformComponent* transform = nullptr;
            Result result = m_world.get_component<WorldTransformComponent>(cameraEntity, transform);
            if (!result || transform == nullptr)
            {
                return;
            }

            CameraComponent* camera = nullptr;
            result = m_world.get_component<CameraComponent>(cameraEntity, camera);
            if (!result || camera == nullptr)
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
                camera->aspectRatio > 0.0f ? camera->aspectRatio : renderAspectRatio;

            // CameraSystem は GameWorld で選択済みの Camera だけを、同期済み WorldTransform から描画入力へ変換する。
            const Math::float4x4 worldMatrix = Math::make_affine_matrix(
                transform->scale,
                transform->rotation,
                transform->position);

            DrawSystem::CameraDrawItem drawItem{};
            // DrawSystem へ渡すのは CameraComponent ではなく、描画用に確定した RenderView。
            drawItem.renderView.view = Math::float4x4::inverse(worldMatrix);
            drawItem.renderView.projection = Math::perspective_fov_matrix(
                camera->fovY * std::numbers::pi_v<float> / 180.0f,
                aspectRatio,
                camera->nearZ,
                camera->farZ);
            drawItem.renderView.position = transform->position;
            drawItem.renderView.width = frameState.renderWidth;
            drawItem.renderView.height = frameState.renderHeight;
            drawItem.renderView.nearZ = camera->nearZ;
            drawItem.renderView.farZ = camera->farZ;

            result = m_drawScenes[a_context.bufferIndex].add_camera(drawItem);
            CUE_ASSERT_FORMAT(success(result), "Failed to add camera to DrawScene: {}", result.message.data());
        }

    private:
        // 描画 Camera の選択は GameWorld が保持する。CameraSystem は走査しない。
        GameCore::GameWorld& m_world;
        // DrawFrameState は描画解像度の参照元。所有権は GameWorld / Engine 側に残す。
        const DrawSystem::DrawFrameState& m_drawFrameState;
        // CameraSystem は frame resource ごとの DrawScene へ選択済み camera item を積むだけ。
        std::vector<DrawSystem::DrawScene>& m_drawScenes;
    };
} // namespace Cue::ECS
