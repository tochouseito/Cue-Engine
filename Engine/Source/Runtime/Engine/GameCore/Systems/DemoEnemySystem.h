// DemoEnemySystem の役割と公開要素を定義する

#pragma once

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <GameCore/Components.h>
#include <GameCore/DebugDraw.h>
#include <GameCore/Navigation/NavComponents.h>

namespace Cue::ECS
{
    class DemoEnemySystem final
        : public ECSManager::System<TransformComponent,
              DemoEnemyComponent,
              NavAgentComponent>
    {
    public:
        explicit DemoEnemySystem(
            GameCore::DebugDrawBuffer* a_debugDraw = nullptr) noexcept
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
            , m_debugDraw(a_debugDraw)
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
                draw_state(a_transform, a_enemy, a_agent);
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
                    draw_state(a_transform, a_enemy, a_agent);
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
                draw_state(a_transform, a_enemy, a_agent);
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
                draw_state(a_transform, a_enemy, a_agent);
                return;
            }

            a_enemy.state = DemoEnemyState::Idle;
            a_agent.hasDestination = false;
            a_agent.hasTarget = false;
            draw_state(a_transform, a_enemy, a_agent);
        }

        [[nodiscard]] static Math::float4 state_color(
            DemoEnemyState a_state) noexcept
        {
            switch (a_state)
            {
            case DemoEnemyState::Patrol:
                return Math::float4(0.1f, 1.0f, 0.25f, 1.0f);
            case DemoEnemyState::MoveToTarget:
                return Math::float4(1.0f, 0.9f, 0.15f, 1.0f);
            case DemoEnemyState::ChasePlayer:
                return Math::float4(1.0f, 0.15f, 0.1f, 1.0f);
            case DemoEnemyState::Idle:
            default:
                return Math::float4(0.65f, 0.65f, 0.65f, 1.0f);
            }
        }

        void draw_state(const TransformComponent& a_transform,
            const DemoEnemyComponent& a_enemy,
            const NavAgentComponent& a_agent)
        {
            if (m_debugDraw == nullptr)
            {
                return;
            }

            constexpr float k_durationSeconds = 0.05f;
            const Math::float4 color = state_color(a_enemy.state);
            m_debugDraw->add_sphere(
                a_transform.position, 0.45f, color, k_durationSeconds);

            if (a_agent.hasDestination && !a_agent.hasTarget)
            {
                m_debugDraw->add_line(a_transform.position,
                    a_agent.destination,
                    color,
                    k_durationSeconds);
                m_debugDraw->add_sphere(
                    a_agent.destination, 0.35f, color, k_durationSeconds);
            }
        }

        GameCore::DebugDrawBuffer* m_debugDraw = nullptr;
    };
}
