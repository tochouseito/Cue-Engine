#pragma once

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <GameCore/Components.h>
#include <GameCore/Navigation/NavComponents.h>

namespace Cue::ECS
{
    class DemoEnemySystem final
        : public ECSManager::System<TransformComponent,
              DemoEnemyComponent,
              NavAgentComponent>
    {
    public:
        DemoEnemySystem()
            : ECSManager::System<TransformComponent,
                  DemoEnemyComponent,
                  NavAgentComponent>(
                  [this](Entity a_entity,
                      TransformComponent& a_transform,
                      DemoEnemyComponent& a_enemy,
                      NavAgentComponent& a_agent,
                      const UpdateContext&)
                  {
                      update_component(
                          a_entity, a_transform, a_enemy, a_agent);
                  })
        {}

    private:
        [[nodiscard]] static bool reached(
            const Math::float3& a_from,
            const Math::float3& a_to,
            float a_distance) noexcept
        {
            const Math::float3 delta = a_to - a_from;
            return delta.length_sq() <= a_distance * a_distance;
        }

        void set_destination(
            NavAgentComponent& a_agent,
            const Math::float3& a_destination) noexcept
        {
            if (a_agent.hasDestination &&
                a_agent.destination.equals_epsilon(a_destination, 0.001f))
            {
                return;
            }

            a_agent.destination = a_destination;
            a_agent.targetEntity = 0;
            a_agent.hasTarget = false;
            a_agent.hasDestination = true;
            a_agent.hasArrived = false;
            a_agent.hasPath = false;
            a_agent.hasPathFailed = false;
            a_agent.pathPoints.clear();
            a_agent.pathIndex = 0;
        }

        void set_target(
            NavAgentComponent& a_agent,
            GameCore::EntityId a_targetEntity) noexcept
        {
            if (a_agent.hasTarget && a_agent.targetEntity == a_targetEntity)
            {
                return;
            }

            a_agent.targetEntity = a_targetEntity;
            a_agent.hasTarget = true;
            a_agent.hasDestination = true;
            a_agent.hasArrived = false;
            a_agent.hasPath = false;
            a_agent.hasPathFailed = false;
            a_agent.pathPoints.clear();
            a_agent.pathIndex = 0;
        }

        void update_component(Entity,
            TransformComponent& a_transform,
            DemoEnemyComponent& a_enemy,
            NavAgentComponent& a_agent)
        {
            if (!a_enemy.isEnabled)
            {
                a_enemy.state = DemoEnemyState::Idle;
                return;
            }

            const TransformComponent* targetTransform = nullptr;
            if (m_pEcs != nullptr &&
                a_enemy.targetEntity != GameCore::k_invalidEntityId)
            {
                targetTransform =
                    m_pEcs->get_component<TransformComponent>(
                        a_enemy.targetEntity);
            }

            if (targetTransform != nullptr)
            {
                const Math::float3 targetDelta =
                    targetTransform->position - a_transform.position;
                if (targetDelta.length_sq() <=
                    a_enemy.chaseDistance * a_enemy.chaseDistance &&
                    targetDelta.length_sq() >
                        a_enemy.stopDistance * a_enemy.stopDistance)
                {
                    a_enemy.state = DemoEnemyState::ChasePlayer;
                    set_target(a_agent, a_enemy.targetEntity);
                    return;
                }
            }

            if (a_enemy.hasRequestedDestination)
            {
                a_enemy.state = DemoEnemyState::MoveToTarget;
                set_destination(a_agent, a_enemy.requestedDestination);
                if (reached(a_transform.position,
                        a_enemy.requestedDestination,
                        a_agent.stoppingDistance))
                {
                    a_enemy.hasRequestedDestination = false;
                    a_enemy.state = DemoEnemyState::Idle;
                }
                return;
            }

            if (!a_enemy.patrolPoints.empty())
            {
                a_enemy.state = DemoEnemyState::Patrol;
                if (a_enemy.patrolIndex >= a_enemy.patrolPoints.size())
                {
                    a_enemy.patrolIndex = 0;
                }

                const Math::float3 destination =
                    a_enemy.patrolPoints[a_enemy.patrolIndex];
                set_destination(a_agent, destination);
                if (reached(a_transform.position,
                        destination,
                        a_agent.stoppingDistance))
                {
                    a_enemy.patrolIndex =
                        (a_enemy.patrolIndex + 1u) %
                        static_cast<uint32_t>(a_enemy.patrolPoints.size());
                }
                return;
            }

            a_enemy.state = DemoEnemyState::Idle;
            a_agent.hasDestination = false;
            a_agent.hasTarget = false;
        }
    };
}
