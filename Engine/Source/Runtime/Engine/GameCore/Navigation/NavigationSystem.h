// NavigationSystem の役割と公開要素を定義する

#pragma once

// === Engine includes ===
#include "NavComponents.h"
#include "NavigationWorld.h"
#include <GameCore/Components.h>

// === C++ includes ===
#include <algorithm>
#include <utility>
#include <vector>

namespace Cue::ECS
{
    class NavigationSystem final
        : public ECSManager::System<TransformComponent, NavAgentComponent>
    {
    public:
        explicit NavigationSystem(
            GameCore::NavigationWorld* a_navigationWorld)
            : ECSManager::System<TransformComponent, NavAgentComponent>(
                [this](Entity a_entity,
                    TransformComponent& a_transform,
                    NavAgentComponent& a_agent,
                    const UpdateContext& a_context)
                {
                    update_component(
                        a_entity, a_transform, a_agent, a_context);
                })
            , m_navigationWorld(a_navigationWorld)
        {}

        void set_navigation_world(
            GameCore::NavigationWorld* a_navigationWorld) noexcept
        {
            m_navigationWorld = a_navigationWorld;
        }

        void set_nav_mesh(GameCore::NavMeshHandle a_handle) noexcept
        {
            m_navMesh = a_handle;
        }

        [[nodiscard]] GameCore::NavMeshHandle nav_mesh() const noexcept
        {
            return m_navMesh;
        }

        void update(const UpdateContext& a_context) override
        {
            m_agentPathLines.clear();
            ECSManager::System<TransformComponent, NavAgentComponent>::update(
                a_context);
        }

        void append_agent_debug_geometry(
            GameCore::NavMeshDebugGeometry& a_outGeometry) const
        {
            a_outGeometry.pathLines.insert(a_outGeometry.pathLines.end(),
                m_agentPathLines.begin(), m_agentPathLines.end());
        }

    private:
        [[nodiscard]] static GameCore::NavQueryFilter make_filter(
            const NavAgentComponent& a_agent) noexcept
        {
            GameCore::NavQueryFilter filter{};
            filter.includeFlags = a_agent.includeFlags;
            filter.excludeFlags = a_agent.excludeFlags;
            return filter;
        }

        [[nodiscard]] static bool reached(
            const Math::float3& a_from,
            const Math::float3& a_to,
            float a_distance) noexcept
        {
            const Math::float3 delta = a_to - a_from;
            return delta.length_sq() <= a_distance * a_distance;
        }

        bool rebuild_path(
            TransformComponent& a_transform,
            NavAgentComponent& a_agent) noexcept
        {
            a_agent.pathPoints.clear();
            a_agent.pathIndex = 0;
            a_agent.hasPath = false;
            a_agent.hasPathFailed = false;
            a_agent.isOnNavMesh = false;

            if (m_navigationWorld == nullptr || !m_navMesh.valid() ||
                !a_agent.hasDestination)
            {
                return false;
            }

            GameCore::NavPath path{};
            const GameCore::NavQueryFilter filter = make_filter(a_agent);
            Math::float3 nearestPoint = Math::float3::zero();
            Result result = m_navigationWorld->find_nearest_point(
                m_navMesh, a_transform.position, nearestPoint);
            if (!result)
            {
                a_agent.hasPathFailed = true;
                return false;
            }

            const Math::float3 nearestDelta = nearestPoint - a_transform.position;
            const float nearestDistance = nearestDelta.length();
            const float snapDistance =
                (std::max)(a_agent.navMeshSnapDistance, 0.0f);
            a_agent.isOnNavMesh = nearestDistance <=
                (std::max)(snapDistance, 0.001f);
            Math::float3 pathStart = a_transform.position;
            if (a_agent.shouldSnapToNavMesh && nearestDistance <= snapDistance)
            {
                pathStart = nearestPoint;
                if (a_agent.movementMode == NavAgentMovementMode::DirectTransform)
                {
                    a_transform.position = nearestPoint;
                }
            }

            result = m_navigationWorld->find_path(m_navMesh,
                pathStart, a_agent.destination, filter, path);
            if (!result || path.points.empty())
            {
                a_agent.hasPathFailed = true;
                return false;
            }

            a_agent.pathPoints = std::move(path.points);
            a_agent.pathIndex = 0;
            a_agent.hasPath = true;
            a_agent.hasPathFailed = false;
            a_agent.lastRequestedDestination = a_agent.destination;

            if (reached(pathStart, a_agent.pathPoints.front(),
                (std::max)(a_agent.stoppingDistance, 0.01f)))
            {
                a_agent.pathIndex = 1;
            }

            return true;
        }

        void finish_path(NavAgentComponent& a_agent) noexcept
        {
            a_agent.pathPoints.clear();
            a_agent.pathIndex = 0;
            a_agent.desiredVelocity = Math::float3::zero();
            a_agent.hasPath = false;
            a_agent.hasDestination = false;
            a_agent.hasArrived = true;
        }

        void update_component(Entity,
            TransformComponent& a_transform,
            NavAgentComponent& a_agent,
            const UpdateContext& a_context) noexcept
        {
            a_agent.desiredVelocity = Math::float3::zero();
            if (a_agent.hasTarget && m_pEcs != nullptr)
            {
                const TransformComponent* targetTransform =
                    m_pEcs->get_component<TransformComponent>(
                        a_agent.targetEntity);
                if (targetTransform != nullptr)
                {
                    a_agent.destination = targetTransform->position;
                    a_agent.destination.y = a_transform.position.y;
                    a_agent.hasDestination = true;
                }
            }

            if (!a_agent.hasDestination || a_context.deltaTime <= 0.0f)
            {
                return;
            }

            a_agent.hasArrived = false;
            const bool needsPath = !a_agent.hasPath ||
                !a_agent.destination.equals_epsilon(
                    a_agent.lastRequestedDestination, 0.001f);
            if (needsPath && !rebuild_path(a_transform, a_agent))
            {
                return;
            }

            if (a_agent.pathIndex >= a_agent.pathPoints.size())
            {
                finish_path(a_agent);
                return;
            }

            append_path_lines(a_agent);

            const float stoppingDistance =
                (std::max)(a_agent.stoppingDistance, 0.001f);
            Math::float3 target = a_agent.pathPoints[a_agent.pathIndex];
            Math::float3 agentPosition = a_transform.position;
            if (a_agent.movementMode ==
                NavAgentMovementMode::DesiredVelocityOnly)
            {
                agentPosition.y = target.y;
            }

            while (reached(agentPosition, target, stoppingDistance))
            {
                ++a_agent.pathIndex;
                if (a_agent.pathIndex >= a_agent.pathPoints.size())
                {
                    finish_path(a_agent);
                    return;
                }
                target = a_agent.pathPoints[a_agent.pathIndex];
                if (a_agent.movementMode ==
                    NavAgentMovementMode::DesiredVelocityOnly)
                {
                    agentPosition.y = target.y;
                }
            }

            Math::float3 delta = target - agentPosition;
            const float distance = delta.length();
            if (distance <= 0.0001f)
            {
                return;
            }

            delta /= distance;
            const float speed = (std::max)(a_agent.maxSpeed, 0.0f);
            const float step = speed * a_context.deltaTime;
            a_agent.desiredVelocity = delta * speed;
            if (a_agent.movementMode ==
                NavAgentMovementMode::DesiredVelocityOnly)
            {
                return;
            }

            if (step >= distance)
            {
                a_transform.position = target;
                ++a_agent.pathIndex;
                return;
            }

            a_transform.position += delta * step;
        }

        void append_path_lines(const NavAgentComponent& a_agent)
        {
            if (a_agent.pathPoints.size() < 2u)
            {
                return;
            }

            for (size_t pointIndex = 1u;
                 pointIndex < a_agent.pathPoints.size(); ++pointIndex)
            {
                GameCore::NavDebugLine line{};
                line.start = a_agent.pathPoints[pointIndex - 1u];
                line.end = a_agent.pathPoints[pointIndex];
                m_agentPathLines.push_back(line);
            }
        }

        GameCore::NavigationWorld* m_navigationWorld = nullptr;
        std::vector<GameCore::NavDebugLine> m_agentPathLines{};
        GameCore::NavMeshHandle m_navMesh{};
    };
}
