// TriggerVolumeSystem の役割と公開要素を定義する

#pragma once

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <GameCore/Components.h>

// === C++ includes ===
#include <algorithm>

namespace Cue::ECS
{
    class TriggerVolumeSystem final
        : public ECSManager::System<WorldTransformComponent,
              ColliderComponent,
              TriggerVolumeComponent>
    {
    public:
        TriggerVolumeSystem()
            : ECSManager::System<WorldTransformComponent,
                  ColliderComponent,
                  TriggerVolumeComponent>(
                  [this](Entity a_entity,
                      WorldTransformComponent& a_transform,
                      ColliderComponent& a_collider,
                      TriggerVolumeComponent& a_trigger,
                      const UpdateContext&)
                  {
                      update_component(
                          a_entity, a_transform, a_collider, a_trigger);
                  })
        {}

    private:
        struct Bounds final
        {
            Math::float3 min = Math::float3::zero();
            Math::float3 max = Math::float3::zero();
        };

        [[nodiscard]] static Math::float3 collider_extent(
            const ColliderComponent& a_collider) noexcept
        {
            switch (a_collider.type)
            {
            case Physics::ShapeType::Sphere:
                return Math::float3(
                    a_collider.radius, a_collider.radius, a_collider.radius);
            case Physics::ShapeType::Capsule:
                return Math::float3(a_collider.radius,
                    a_collider.halfHeight + a_collider.radius,
                    a_collider.radius);
            case Physics::ShapeType::Box:
            case Physics::ShapeType::Mesh:
            default:
                return a_collider.halfExtent;
            }
        }

        [[nodiscard]] static Bounds make_bounds(
            const WorldTransformComponent& a_transform,
            const ColliderComponent& a_collider) noexcept
        {
            const Math::float3 center = a_transform.position + a_collider.offset;
            const Math::float3 extent = collider_extent(a_collider);
            return Bounds{ center - extent, center + extent };
        }

        [[nodiscard]] static bool overlaps(
            const Bounds& a_left,
            const Bounds& a_right) noexcept
        {
            return a_left.min.x <= a_right.max.x &&
                a_left.max.x >= a_right.min.x &&
                a_left.min.y <= a_right.max.y &&
                a_left.max.y >= a_right.min.y &&
                a_left.min.z <= a_right.max.z &&
                a_left.max.z >= a_right.min.z;
        }

        void update_component(Entity a_entity,
            WorldTransformComponent& a_transform,
            ColliderComponent& a_collider,
            TriggerVolumeComponent& a_trigger)
        {
            if (m_pEcs == nullptr)
            {
                return;
            }

            const std::vector<GameCore::EntityId> previous =
                a_trigger.overlappingEntities;
            a_trigger.overlappingEntities.clear();
            a_trigger.enteredEntities.clear();
            a_trigger.exitedEntities.clear();

            const Bounds triggerBounds = make_bounds(a_transform, a_collider);
            for (auto& [arch, bucket] : m_pEcs->get_arch_to_entities())
            {
                arch;
                for (Entity candidate : bucket.get_entities())
                {
                    if (candidate == a_entity ||
                        !m_pEcs->is_entity_active(candidate))
                    {
                        continue;
                    }

                    const WorldTransformComponent* transform =
                        m_pEcs->get_component<WorldTransformComponent>(candidate);
                    const ColliderComponent* collider =
                        m_pEcs->get_component<ColliderComponent>(candidate);
                    if (transform == nullptr || collider == nullptr)
                    {
                        continue;
                    }
                    if (!a_trigger.includeTriggers && collider->isTrigger)
                    {
                        continue;
                    }
                    if (!overlaps(triggerBounds, make_bounds(*transform, *collider)))
                    {
                        continue;
                    }

                    a_trigger.overlappingEntities.push_back(candidate);
                    if (std::find(previous.begin(), previous.end(),
                            candidate) == previous.end())
                    {
                        a_trigger.enteredEntities.push_back(candidate);
                    }
                }
            }

            for (GameCore::EntityId entity : previous)
            {
                if (std::find(a_trigger.overlappingEntities.begin(),
                        a_trigger.overlappingEntities.end(),
                        entity) == a_trigger.overlappingEntities.end())
                {
                    a_trigger.exitedEntities.push_back(entity);
                }
            }
        }
    };
}
