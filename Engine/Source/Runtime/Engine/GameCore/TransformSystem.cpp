#include "TransformSystem.h"

// === Engine includes ===
#include "GameWorld.h"

// === C++ includes ===
#include <cmath>
#include <limits>
#include <vector>

namespace Cue::GameCore
{
    ECS::WorldTransformComponent TransformSystem::compose_world_transform(
            const ECS::WorldTransformComponent& a_parent,
            const ECS::TransformComponent& a_local) noexcept
        {
            ECS::WorldTransformComponent world{};
            world.scale = Math::float3(
                a_parent.scale.x * a_local.scale.x,
                a_parent.scale.y * a_local.scale.y,
                a_parent.scale.z * a_local.scale.z);
            world.rotation = Math::Quaternion::normalize(a_parent.rotation * a_local.rotation);

            const Math::float3 scaledLocalPosition(
                a_local.position.x * a_parent.scale.x,
                a_local.position.y * a_parent.scale.y,
                a_local.position.z * a_parent.scale.z);
            world.position = a_parent.position + rotate_vector(a_parent.rotation, scaledLocalPosition);
            return world;
        }

    ECS::TransformComponent TransformSystem::make_local_transform(
            const ECS::WorldTransformComponent& a_parent,
            const ECS::WorldTransformComponent& a_world) noexcept
        {
            const auto divideSafe = [](float a_value, float a_divisor) noexcept
            {
                return std::abs(a_divisor) > std::numeric_limits<float>::epsilon()
                    ? a_value / a_divisor
                    : 0.0f;
            };

            ECS::TransformComponent local{};
            const Math::Quaternion inverseParentRotation = Math::Quaternion::inverse(a_parent.rotation);
            const Math::float3 unrotatedPosition =
                rotate_vector(inverseParentRotation, a_world.position - a_parent.position);
            local.position = Math::float3(
                divideSafe(unrotatedPosition.x, a_parent.scale.x),
                divideSafe(unrotatedPosition.y, a_parent.scale.y),
                divideSafe(unrotatedPosition.z, a_parent.scale.z));
            local.rotation = Math::Quaternion::normalize(inverseParentRotation * a_world.rotation);
            local.scale = Math::float3(
                divideSafe(a_world.scale.x, a_parent.scale.x),
                divideSafe(a_world.scale.y, a_parent.scale.y),
                divideSafe(a_world.scale.z, a_parent.scale.z));
            return local;
        }

    bool TransformSystem::resolve_world_transform(
            GameWorld& a_world,
            EntityId a_entityId,
            std::vector<uint8_t>& a_state,
            ECS::WorldTransformComponent& a_outWorld) noexcept
        {
            if (!a_world.contains_object(a_entityId) || a_entityId >= a_state.size())
            {
                return false;
            }

            uint8_t& state = a_state[a_entityId];
            if (state == 2u)
            {
                ECS::WorldTransformComponent* resolved =
                    a_world.m_ecsManager.get_component<ECS::WorldTransformComponent>(a_entityId);
                if (resolved == nullptr)
                {
                    return false;
                }
                a_outWorld = *resolved;
                return true;
            }
            if (state == 1u)
            {
                return false;
            }

            ECS::TransformComponent* local =
                a_world.m_ecsManager.get_component<ECS::TransformComponent>(a_entityId);
            if (local == nullptr)
            {
                return false;
            }

            ECS::WorldTransformComponent* world =
                a_world.m_ecsManager.get_component<ECS::WorldTransformComponent>(a_entityId);
            if (world == nullptr)
            {
                world = a_world.m_ecsManager.add_component<ECS::WorldTransformComponent>(a_entityId);
                if (world == nullptr)
                {
                    return false;
                }
            }

            state = 1u;
            const BaseComponent* base = a_world.m_ecsManager.get_component<BaseComponent>(a_entityId);
            const EntityId parent = base != nullptr ? base->parent : k_invalidEntityId;
            if (parent != k_invalidEntityId && a_world.contains_object(parent) &&
                a_world.m_ecsManager.get_component<ECS::TransformComponent>(parent) != nullptr)
            {
                ECS::WorldTransformComponent parentWorld{};
                if (resolve_world_transform(a_world, parent, a_state, parentWorld))
                {
                    *world = compose_world_transform(parentWorld, *local);
                }
                else
                {
                    world->position = local->position;
                    world->rotation = local->rotation;
                    world->scale = local->scale;
                }
            }
            else
            {
                world->position = local->position;
                world->rotation = local->rotation;
                world->scale = local->scale;
            }

            state = 2u;
            a_outWorld = *world;
            return true;
        }
    Result TransformSystem::set_parent(GameWorld& a_world, EntityId a_childEntityId,
                                       EntityId a_parentEntityId, bool a_keepsWorldTransform) noexcept
    {
        if (!a_world.contains_object(a_childEntityId) || !a_world.contains_object(a_parentEntityId))
        {
            return Result::fail(Code::InvalidState, Severity::Warning, "GameWorld object is not alive.");
        }
        if (a_childEntityId == a_parentEntityId)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "GameWorld parent cannot be the child itself.");
        }
        if (a_world.is_descendant_of(a_parentEntityId, a_childEntityId))
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "GameWorld parent cycle was rejected.");
        }

        ECS::TransformComponent* childTransform =
            a_world.m_ecsManager.get_component<ECS::TransformComponent>(a_childEntityId);
        ECS::TransformComponent* parentTransform =
            a_world.m_ecsManager.get_component<ECS::TransformComponent>(a_parentEntityId);
        if (childTransform == nullptr || parentTransform == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Warning,
                                "GameWorld parent update requires TransformComponent.");
        }

        ECS::WorldTransformComponent childWorld{};
        ECS::WorldTransformComponent parentWorld{};
        if (a_keepsWorldTransform)
        {
            std::vector<uint8_t> state(a_world.m_entityRecords.size(), 0u);
            if (!resolve_world_transform(a_world, a_childEntityId, state, childWorld) ||
                !resolve_world_transform(a_world, a_parentEntityId, state, parentWorld))
            {
                return Result::fail(Code::InvalidState, Severity::Error,
                                    "GameWorld world transform could not be resolved.");
            }
        }

        BaseComponent* childBase = a_world.m_ecsManager.get_component<BaseComponent>(a_childEntityId);
        if (childBase == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error, "GameWorld BaseComponent is missing.");
        }

        childBase->parent = a_parentEntityId;
        if (a_keepsWorldTransform)
        {
            *childTransform = make_local_transform(parentWorld, childWorld);
        }
        sync_world_transforms(a_world);
        return Result::ok();
    }

    Result TransformSystem::detach_parent(GameWorld& a_world, EntityId a_childEntityId,
                                          bool a_keepsWorldTransform) noexcept
    {
        if (!a_world.contains_object(a_childEntityId))
        {
            return Result::fail(Code::InvalidState, Severity::Warning, "GameWorld object is not alive.");
        }

        BaseComponent* childBase = a_world.m_ecsManager.get_component<BaseComponent>(a_childEntityId);
        ECS::TransformComponent* childTransform =
            a_world.m_ecsManager.get_component<ECS::TransformComponent>(a_childEntityId);
        if (childBase == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error, "GameWorld BaseComponent is missing.");
        }
        if (childTransform == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Warning,
                                "GameWorld parent detach requires TransformComponent.");
        }

        ECS::WorldTransformComponent childWorld{};
        if (a_keepsWorldTransform)
        {
            std::vector<uint8_t> state(a_world.m_entityRecords.size(), 0u);
            if (!resolve_world_transform(a_world, a_childEntityId, state, childWorld))
            {
                return Result::fail(Code::InvalidState, Severity::Error,
                                    "GameWorld world transform could not be resolved.");
            }
        }

        childBase->parent = k_invalidEntityId;
        if (a_keepsWorldTransform)
        {
            childTransform->position = childWorld.position;
            childTransform->rotation = childWorld.rotation;
            childTransform->scale = childWorld.scale;
        }
        sync_world_transforms(a_world);
        return Result::ok();
    }

    void TransformSystem::sync_world_transforms(GameWorld& a_world) noexcept
    {
        std::vector<uint8_t> state(a_world.m_entityRecords.size(), 0u);
        for (EntityId entity = 0; entity < static_cast<EntityId>(a_world.m_entityRecords.size()); ++entity)
        {
            if (!a_world.contains_object(entity) ||
                a_world.m_ecsManager.get_component<ECS::TransformComponent>(entity) == nullptr)
            {
                continue;
            }

            ECS::WorldTransformComponent world{};
            (void)resolve_world_transform(a_world, entity, state, world);
        }
    }
} // namespace Cue::GameCore
