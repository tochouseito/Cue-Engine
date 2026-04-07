#pragma once

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <GameCore/Components.h>

namespace Cue::ECS
{
    class TransformSystem final : public ECSManager::System<TransformComponent>
    {
    public:
        TransformSystem()
            :ECSManager::System<TransformComponent>(
                [&](Entity e, TransformComponent& transform)
                {
                    update_component(e, transform);
                },
                [&](Entity e, TransformComponent& transform)
                {
                    initialize_component(e, transform);
                },
                [&](Entity e, TransformComponent& transform)
                {
                    finalize_component(e, transform);
                })
        {}
    private:
        void update_component(Entity e, TransformComponent& transform);
        void initialize_component(Entity e, TransformComponent& transform);
        void finalize_component(Entity e, TransformComponent& transform);
    };
}
