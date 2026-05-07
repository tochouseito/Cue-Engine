#pragma once

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <GameCore/Components.h>

// === Physics includes ===
#include <Physics.h>

// === C++ includes ===
#include <algorithm>
#include <array>
#include <cmath>

namespace Cue::ECS
{
    class CharacterControllerSystem final
        : public ECSManager::System<TransformComponent,
              RigidBodyComponent,
              ColliderComponent,
              CharacterControllerComponent>
    {
    public:
        explicit CharacterControllerSystem(
            Physics::IPhysicsSystem* a_physicsSystem)
            : ECSManager::System<TransformComponent,
                  RigidBodyComponent,
                  ColliderComponent,
                  CharacterControllerComponent>(
                  [this](Entity a_entity,
                      TransformComponent& a_transform,
                      RigidBodyComponent& a_rigidBody,
                      ColliderComponent& a_collider,
                      CharacterControllerComponent& a_controller,
                      const UpdateContext& a_context)
                  {
                      update_component(a_entity, a_transform, a_rigidBody,
                          a_collider, a_controller, a_context);
                  },
                  [this](Entity a_entity,
                      TransformComponent& a_transform,
                      RigidBodyComponent& a_rigidBody,
                      ColliderComponent& a_collider,
                      CharacterControllerComponent& a_controller,
                      const InitializeContext& a_context)
                  {
                      initialize_component(a_entity, a_transform, a_rigidBody,
                          a_collider, a_controller, a_context);
                  }),
            m_physicsSystem(a_physicsSystem)
        {}

    private:
        void initialize_component(Entity a_entity,
            TransformComponent& a_transform,
            RigidBodyComponent& a_rigidBody,
            ColliderComponent& a_collider,
            CharacterControllerComponent& a_controller,
            const InitializeContext& a_context)
        {
            a_entity;
            a_transform;
            a_collider;
            a_controller;
            a_context;

            if (!a_rigidBody.isCreated)
            {
                a_rigidBody.motion = Physics::MotionType::Kinematic;
                a_rigidBody.useGravity = false;
            }
        }

        void update_component(Entity a_entity,
            TransformComponent& a_transform,
            RigidBodyComponent& a_rigidBody,
            ColliderComponent& a_collider,
            CharacterControllerComponent& a_controller,
            const UpdateContext& a_context)
        {
            a_entity;

            if (a_context.deltaTime <= 0.0f ||
                a_rigidBody.motion != Physics::MotionType::Kinematic)
            {
                a_controller.jumpRequested = false;
                return;
            }

            const float footHeight = compute_foot_height(a_collider);
            update_ground_state(
                a_transform, a_rigidBody, a_collider, a_controller, footHeight);

            if (a_controller.isGrounded &&
                a_controller.verticalVelocity <= 0.0f)
            {
                a_controller.verticalVelocity = 0.0f;
            }
            if (a_controller.jumpRequested && a_controller.isGrounded)
            {
                a_controller.verticalVelocity = a_controller.jumpSpeed;
                a_controller.isGrounded = false;
            }
            a_controller.jumpRequested = false;

            a_controller.verticalVelocity -=
                a_controller.gravity * a_context.deltaTime;

            const Math::float3 horizontalVelocity = clamp_horizontal_velocity(
                a_controller.moveVelocity, a_controller.maxSpeed);
            move_horizontal(a_transform, a_rigidBody, a_collider,
                horizontalVelocity, a_context.deltaTime);
            a_transform.position.y +=
                a_controller.verticalVelocity * a_context.deltaTime;
            snap_to_ground(
                a_transform, a_rigidBody, a_collider, a_controller, footHeight);
        }

        void move_horizontal(TransformComponent& a_transform,
            const RigidBodyComponent& a_rigidBody,
            const ColliderComponent& a_collider,
            const Math::float3& a_velocity,
            float a_deltaTime) const
        {
            Math::float3 horizontalStep = a_velocity * a_deltaTime;
            horizontalStep.y = 0.0f;
            const float stepLength = horizontalStep.length();
            if (stepLength <= 0.0001f)
            {
                return;
            }

            Math::float3 direction = horizontalStep / stepLength;
            if (m_physicsSystem == nullptr || !a_rigidBody.body.valid())
            {
                a_transform.position += horizontalStep;
                return;
            }

            const float radius = horizontal_radius(a_collider);
            const float skinWidth = collider_skin_width(a_collider);
            const float castDistance = stepLength + radius +
                (std::max)(skinWidth, 0.001f);
            const Math::float3 baseOrigin =
                a_transform.position + a_collider.offset;
            const float footHeight = compute_foot_height(a_collider);
            const float upperOffset = (std::max)(footHeight * 0.75f, 0.1f);

            const std::array<Math::float3, 3> origins{
                baseOrigin,
                baseOrigin + Math::float3(0.0f, upperOffset, 0.0f),
                baseOrigin - Math::float3(0.0f, upperOffset * 0.5f, 0.0f),
            };

            float allowedStep = stepLength;
            for (const Math::float3& origin : origins)
            {
                Physics::RaycastDesc raycast{};
                raycast.origin = origin;
                raycast.direction = direction;
                raycast.ignoredBody = a_rigidBody.body;
                raycast.distance = castDistance;

                Physics::RaycastHit hit{};
                const Result result = m_physicsSystem->raycast(raycast, hit);
                if (!result)
                {
                    continue;
                }

                const float hitStep = hit.distance - radius - skinWidth;
                allowedStep = (std::min)(allowedStep, (std::max)(hitStep, 0.0f));
            }

            a_transform.position += direction * allowedStep;
        }

        void update_ground_state(
            const TransformComponent& a_transform,
            const RigidBodyComponent& a_rigidBody,
            const ColliderComponent& a_collider,
            CharacterControllerComponent& a_controller,
            float a_footHeight) const
        {
            a_controller.isGrounded = false;
            if (m_physicsSystem == nullptr)
            {
                return;
            }

            Physics::RaycastDesc raycast{};
            raycast.origin = a_transform.position + a_collider.offset;
            raycast.direction = Math::float3(0.0f, -1.0f, 0.0f);
            raycast.ignoredBody = a_rigidBody.body;
            raycast.distance =
                a_footHeight + a_controller.groundCheckDistance +
                a_controller.skinWidth;

            Physics::RaycastHit hit{};
            const Result result = m_physicsSystem->raycast(raycast, hit);
            if (!result)
            {
                return;
            }

            const float groundY =
                hit.position.y - a_collider.offset.y + a_footHeight;
            const float footY = a_transform.position.y;
            a_controller.isGrounded =
                footY <= groundY + a_controller.groundCheckDistance;
        }

        void snap_to_ground(TransformComponent& a_transform,
            const RigidBodyComponent& a_rigidBody,
            const ColliderComponent& a_collider,
            CharacterControllerComponent& a_controller,
            float a_footHeight) const
        {
            if (!a_controller.isGrounded ||
                a_controller.verticalVelocity > 0.0f ||
                m_physicsSystem == nullptr)
            {
                return;
            }

            Physics::RaycastDesc raycast{};
            raycast.origin = a_transform.position + a_collider.offset;
            raycast.direction = Math::float3(0.0f, -1.0f, 0.0f);
            raycast.ignoredBody = a_rigidBody.body;
            raycast.distance =
                a_footHeight + a_controller.groundCheckDistance +
                a_controller.skinWidth;

            Physics::RaycastHit hit{};
            const Result result = m_physicsSystem->raycast(raycast, hit);
            if (!result)
            {
                a_controller.isGrounded = false;
                return;
            }

            a_transform.position.y =
                hit.position.y - a_collider.offset.y + a_footHeight;
        }

        [[nodiscard]] static Math::float3 clamp_horizontal_velocity(
            Math::float3 a_velocity,
            float a_maxSpeed) noexcept
        {
            a_velocity.y = 0.0f;
            const float lengthSquared =
                a_velocity.x * a_velocity.x + a_velocity.z * a_velocity.z;
            const float maxSpeed = (std::max)(a_maxSpeed, 0.0f);
            const float maxSpeedSquared = maxSpeed * maxSpeed;
            if (lengthSquared <= maxSpeedSquared || lengthSquared <= 0.0f)
            {
                return a_velocity;
            }

            const float scale = maxSpeed / std::sqrt(lengthSquared);
            return Math::float3(
                a_velocity.x * scale, 0.0f, a_velocity.z * scale);
        }

        [[nodiscard]] static float compute_foot_height(
            const ColliderComponent& a_collider) noexcept
        {
            switch (a_collider.type)
            {
            case Physics::ShapeType::Sphere:
                return (std::max)(a_collider.radius, 0.001f);
            case Physics::ShapeType::Capsule:
                return (std::max)(a_collider.halfHeight, 0.001f) +
                    (std::max)(a_collider.radius, 0.001f);
            case Physics::ShapeType::Box:
            case Physics::ShapeType::Mesh:
            default:
                return (std::max)(a_collider.halfExtent.y, 0.001f);
            }
        }

        [[nodiscard]] static float horizontal_radius(
            const ColliderComponent& a_collider) noexcept
        {
            switch (a_collider.type)
            {
            case Physics::ShapeType::Sphere:
            case Physics::ShapeType::Capsule:
                return (std::max)(a_collider.radius, 0.001f);
            case Physics::ShapeType::Box:
            case Physics::ShapeType::Mesh:
            default:
                return (std::max)(
                    (std::max)(a_collider.halfExtent.x, a_collider.halfExtent.z),
                    0.001f);
            }
        }

        [[nodiscard]] static float collider_skin_width(
            const ColliderComponent&) noexcept
        {
            return 0.03f;
        }

    private:
        Physics::IPhysicsSystem* m_physicsSystem = nullptr;
    };
}
