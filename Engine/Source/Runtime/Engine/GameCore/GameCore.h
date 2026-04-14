#pragma once

// === Base includes ===
#include <Result.h>

// === Math includes ===
#include <CueMath.h>

// === Engine includes ===
#include "RenderSceneState.h"
#include "WorldResources.h"

// === ECS includes ===
#include "Systems/CameraSystem.h"
#include "Systems/ObjectInfoSystem.h"
#include "Systems/TransformSystem.h"

// === C++ includes ===
#include <memory>
#include <vector>

namespace Cue
{
    class GameCoreLegacy final
    {
    public:
        GameCoreLegacy() = default;
        ~GameCoreLegacy() = default;

        Result initialize(RHI::IBufferManager* a_bufferManager,
            RHI::IViewManager* a_viewManager, uint32_t a_bufferCount,
            uint32_t a_renderWidth, uint32_t a_renderHeight);
        Result update(float a_deltaTime, const uint32_t a_bufferIndex,
            uint32_t a_renderWidth, uint32_t a_renderHeight);

        Result add_object();
        Result add_object(const Math::float3& a_position);
        Result remove_object(uint32_t objectId);
        Result set_main_camera(uint32_t a_cameraIndex);

        RenderSceneState& render_scene_state() noexcept
        {
            return m_renderSceneState;
        }

        const RenderSceneState& render_scene_state() const noexcept
        {
            return m_renderSceneState;
        }

    private:
        [[nodiscard]] Math::float3 make_spawn_position() const noexcept;
        Result create_camera(const Math::float3& a_position, bool a_isMain);
        Result create_default_cameras();
        void rebuild_object_indices();
        void sync_render_scene_state(uint32_t a_bufferIndex, uint32_t a_renderWidth,
            uint32_t a_renderHeight) noexcept;

    private:
        std::unique_ptr<ECS::ECSManager> m_ecsManager = nullptr;
        std::unique_ptr<WorldResources> m_worldResources = nullptr;
        RenderSceneState m_renderSceneState{};
        std::vector<ECS::Entity> m_entities{};
        std::vector<ECS::Entity> m_cameraEntities{};
        uint32_t m_objectCount = 0;
        uint32_t m_mainCameraIndex = 0;
    };
} // namespace Cue
