// NavComponents の役割と公開要素を定義する

#pragma once

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include "NavTypes.h"

namespace Cue::ECS
{
    enum class NavAgentMovementMode : uint8_t
    {
        DirectTransform,
        DesiredVelocityOnly,
    };

    struct NavAgentComponent final : public IComponentTag
    {
        float radius = 0.3f;
        float height = 1.8f;
        float maxSpeed = 3.0f;
        float acceleration = 8.0f;
        float stoppingDistance = 0.1f;
        float navMeshSnapDistance = 1.0f;
        uint16_t includeFlags = 0xFFFFu;
        uint16_t excludeFlags = 0;
        Math::float3 destination = Math::float3::zero();
        Math::float3 lastRequestedDestination = Math::float3::zero();
        Math::float3 desiredVelocity = Math::float3::zero();
        std::vector<Math::float3> pathPoints{};
        Entity targetEntity = 0;
        uint32_t pathIndex = 0;
        NavAgentMovementMode movementMode = NavAgentMovementMode::DirectTransform;
        bool shouldSnapToNavMesh = true;
        bool hasDestination = false;
        bool hasTarget = false;
        bool hasPath = false;
        bool hasArrived = false;
        bool hasPathFailed = false;
        bool isOnNavMesh = false;
    };

    struct NavMeshBakeSourceComponent final : public IComponentTag
    {
        uint8_t area = static_cast<uint8_t>(GameCore::NavAreaType::Walkable);
        bool isIncluded = true;
    };
}
