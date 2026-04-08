#pragma once

// === Base includes ===
#include <Result.h>

// === Math includes ===
#include <CueMath.h>

// === Engine includes ===
#include "RenderSceneState.h"
#include "WorldResources.h"

// === ECS includes ===
#include "Systems/ObjectInfoSystem.h"
#include "Systems/TransformSystem.h"

// === C++ includes ===
#include <memory>
#include <vector>

namespace Cue
{
    class GameCore final
    {
    public:
        GameCore() = default;
        ~GameCore() = default;

        Result initialize(RHI::IBufferManager* a_bufferManager,
            RHI::IViewManager* a_viewManager, uint32_t a_bufferCount);
        Result update(float a_deltaTime, const uint32_t a_bufferIndex);

        Result add_object();
        Result add_object(const Math::float3& a_position);
        Result remove_object(uint32_t objectId);

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
        void rebuild_object_indices();
        void sync_render_scene_state(uint32_t a_bufferIndex) noexcept;

    private:
        std::unique_ptr<ECS::ECSManager> m_ecsManager = nullptr;
        std::unique_ptr<WorldResources> m_worldResources = nullptr;
        RenderSceneState m_renderSceneState{};
        std::vector<ECS::Entity> m_entities{};
        uint32_t m_objectCount = 0;
    };
} // namespace Cue
