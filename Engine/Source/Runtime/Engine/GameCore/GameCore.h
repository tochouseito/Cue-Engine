#pragma once

// === Base includes ===
#include <Result.h>

// === Math includes ===
#include <CueMath.h>

// === ECS includes ===
#include "Systems/ObjectInfoSystem.h"
#include "Systems/TransformSystem.h"

// === Engine includes ===
#include "RenderSceneState.h"

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

        Result initialize();
        Result update(float a_deltaTime);

        Result add_object(const Math::float3& a_position);
        Result remove_object(uint32_t objectId);

        const RenderSceneState& render_scene_state() const noexcept
        {
            return m_renderSceneState;
        }

    private:
        std::unique_ptr<ECS::ECSManager> m_ecsManager = nullptr;
        RenderSceneState m_renderSceneState{};
        std::vector<ECS::Entity> m_entities{};
    };
} // namespace Cue
