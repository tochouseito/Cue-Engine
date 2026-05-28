// NavAgentMotorSystem の役割と公開要素を定義する

#pragma once

// === Engine includes ===
#include "NavComponents.h"
#include <GameCore/Components.h>

namespace Cue::ECS
{
    class NavAgentMotorSystem final
        : public ECSManager::System<NavAgentComponent, CharacterControllerComponent>
    {
    public:
        NavAgentMotorSystem()
            : ECSManager::System<NavAgentComponent, CharacterControllerComponent>(
                [](Entity,
                    NavAgentComponent& a_agent,
                    CharacterControllerComponent& a_controller,
                    const UpdateContext&)
                {
                    if (a_agent.movementMode !=
                        NavAgentMovementMode::DesiredVelocityOnly)
                    {
                        return;
                    }

                    a_controller.moveVelocity = a_agent.desiredVelocity;
                })
        {}
    };
}
