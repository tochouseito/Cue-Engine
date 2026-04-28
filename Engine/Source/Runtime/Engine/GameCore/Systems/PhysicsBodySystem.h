#pragma once

// === Base includes ===
#include <CueAssert.h>

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <GameCore/Components.h>

// === Physics includes ===
#include <Physics.h>

// === C++ includes ===
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Cue::ECS
{
    class PhysicsBodySystem final
        : public ECSManager::System<TransformComponent,
              RigidBodyComponent,
              ColliderComponent>
    {
    public:
        explicit PhysicsBodySystem(Physics::IPhysicsSystem* a_physicsSystem)
            : ECSManager::System<TransformComponent,
                  RigidBodyComponent,
                  ColliderComponent>(
                  [this](Entity a_entity,
                      TransformComponent& a_transform,
                      RigidBodyComponent& a_rigidBody,
                      ColliderComponent& a_collider,
                      const UpdateContext& a_context)
                  {
                      update_component(
                          a_entity, a_transform, a_rigidBody, a_collider,
                          a_context);
                  },
                  [this](Entity a_entity,
                      TransformComponent& a_transform,
                      RigidBodyComponent& a_rigidBody,
                      ColliderComponent& a_collider,
                      const InitializeContext& a_context)
                  {
                      initialize_component(
                          a_entity, a_transform, a_rigidBody, a_collider,
                          a_context);
                  },
                  [this](Entity a_entity,
                      TransformComponent& a_transform,
                      RigidBodyComponent& a_rigidBody,
                      ColliderComponent& a_collider,
                      const FinalizeContext& a_context)
                  {
                      finalize_component(
                          a_entity, a_transform, a_rigidBody, a_collider,
                          a_context);
                  }),
            m_physicsSystem(a_physicsSystem)
        {}

        void update(const UpdateContext& a_context) override
        {
            if (m_physicsSystem == nullptr)
            {
                return;
            }

            m_phase = Phase::BeforeStep;
            ECSManager::System<TransformComponent,
                RigidBodyComponent,
                ColliderComponent>::update(a_context);

            Physics::PhysicsStepDesc stepDesc{};
            stepDesc.deltaTime = a_context.deltaTime;
            const Result stepResult = m_physicsSystem->step(stepDesc);
            if (!stepResult)
            {
                CUE_ASSERTF(false, "Physics step failed: %s",
                    stepResult.message.data());
                return;
            }

            m_phase = Phase::AfterStep;
            ECSManager::System<TransformComponent,
                RigidBodyComponent,
                ColliderComponent>::update(a_context);
        }

    private:
        enum class Phase : uint8_t
        {
            BeforeStep,
            AfterStep
        };

        void initialize_component(Entity a_entity,
            TransformComponent& a_transform,
            RigidBodyComponent& a_rigidBody,
            ColliderComponent& a_collider,
            const InitializeContext& a_context)
        {
            a_entity;
            a_transform;
            a_collider;
            a_context;
            a_rigidBody.body = {};
            a_rigidBody.isCreated = false;
        }

        void update_component(Entity a_entity,
            TransformComponent& a_transform,
            RigidBodyComponent& a_rigidBody,
            ColliderComponent& a_collider,
            const UpdateContext& a_context)
        {
            a_entity;
            a_context;

            if (m_phase == Phase::BeforeStep)
            {
                ensure_body(a_transform, a_rigidBody, a_collider);
                push_kinematic_transform(a_transform, a_rigidBody, a_collider);
                return;
            }

            pull_dynamic_transform(a_transform, a_rigidBody, a_collider);
        }

        void finalize_component(Entity a_entity,
            TransformComponent& a_transform,
            RigidBodyComponent& a_rigidBody,
            ColliderComponent& a_collider,
            const FinalizeContext& a_context)
        {
            a_entity;
            a_transform;
            a_collider;
            a_context;
            destroy_body(a_rigidBody);
        }

        void ensure_body(TransformComponent& a_transform,
            RigidBodyComponent& a_rigidBody,
            ColliderComponent& a_collider)
        {
            if (a_rigidBody.isCreated || m_physicsSystem == nullptr)
            {
                return;
            }

            Physics::RigidBodyDesc desc{};
            desc.shape.type = a_collider.type;
            desc.shape.halfExtent = sanitize_half_extent(a_collider.halfExtent);
            desc.shape.radius = (std::max)(a_collider.radius, 0.001f);
            desc.shape.halfHeight = (std::max)(a_collider.halfHeight, 0.001f);
            desc.position = a_transform.position + a_collider.offset;
            desc.rotation = euler_to_quat(a_transform.rotation);
            desc.linearVelocity = a_rigidBody.linearVelocity;
            desc.angularVelocity = a_rigidBody.angularVelocity;
            desc.motion = a_rigidBody.motion;
            desc.mass = (std::max)(a_rigidBody.mass, 0.001f);
            desc.linearDamping = (std::max)(a_rigidBody.linearDamping, 0.0f);
            desc.angularDamping = (std::max)(a_rigidBody.angularDamping, 0.0f);
            desc.gravityFactor = a_rigidBody.useGravity ? 1.0f : 0.0f;
            desc.friction = a_collider.friction;
            desc.restitution = a_collider.restitution;
            desc.collisionLayer = a_collider.layer;
            desc.isSensor = a_collider.isTrigger;

            Physics::RigidBodyHandle body{};
            const Result result = m_physicsSystem->create_body(desc, body);
            if (!result)
            {
                CUE_ASSERTF(false, "Physics body creation failed: %s",
                    result.message.data());
                return;
            }

            a_rigidBody.body = body;
            a_rigidBody.isCreated = true;
        }

        void push_kinematic_transform(TransformComponent& a_transform,
            RigidBodyComponent& a_rigidBody,
            ColliderComponent& a_collider)
        {
            if (m_physicsSystem == nullptr || !a_rigidBody.isCreated ||
                a_rigidBody.motion != Physics::MotionType::Kinematic)
            {
                return;
            }

            Physics::BodyTransform transform{};
            transform.position = a_transform.position + a_collider.offset;
            transform.rotation = euler_to_quat(a_transform.rotation);
            const Result result = m_physicsSystem->set_body_transform(
                a_rigidBody.body, transform, Physics::BodyActivation::Activate);
            if (!result)
            {
                CUE_ASSERTF(false, "Physics kinematic sync failed: %s",
                    result.message.data());
            }
        }

        void pull_dynamic_transform(TransformComponent& a_transform,
            RigidBodyComponent& a_rigidBody,
            ColliderComponent& a_collider)
        {
            if (m_physicsSystem == nullptr || !a_rigidBody.isCreated ||
                a_rigidBody.motion != Physics::MotionType::Dynamic)
            {
                return;
            }

            Physics::BodyTransform transform{};
            const Result result =
                m_physicsSystem->get_body_transform(a_rigidBody.body, transform);
            if (!result)
            {
                CUE_ASSERTF(false, "Physics transform sync failed: %s",
                    result.message.data());
                return;
            }

            a_transform.position = transform.position - a_collider.offset;
            a_transform.rotation = quat_to_euler(transform.rotation);
        }

        void destroy_body(RigidBodyComponent& a_rigidBody)
        {
            if (m_physicsSystem == nullptr || !a_rigidBody.isCreated)
            {
                a_rigidBody.body = {};
                a_rigidBody.isCreated = false;
                return;
            }

            const Result result = m_physicsSystem->destroy_body(a_rigidBody.body);
            if (!result)
            {
                CUE_ASSERTF(false, "Physics body destruction failed: %s",
                    result.message.data());
            }

            a_rigidBody.body = {};
            a_rigidBody.isCreated = false;
        }

        [[nodiscard]] static Math::float3 sanitize_half_extent(
            Math::float3 a_halfExtent) noexcept
        {
            return Math::float3(
                (std::max)(a_halfExtent.x, 0.001f),
                (std::max)(a_halfExtent.y, 0.001f),
                (std::max)(a_halfExtent.z, 0.001f));
        }

        [[nodiscard]] static Math::float4 euler_to_quat(
            Math::float3 a_euler) noexcept
        {
            const float halfX = a_euler.x * 0.5f;
            const float halfY = a_euler.y * 0.5f;
            const float halfZ = a_euler.z * 0.5f;
            const float sx = std::sin(halfX);
            const float cx = std::cos(halfX);
            const float sy = std::sin(halfY);
            const float cy = std::cos(halfY);
            const float sz = std::sin(halfZ);
            const float cz = std::cos(halfZ);

            Math::float4 result{};
            result.x = sx * cy * cz + cx * sy * sz;
            result.y = cx * sy * cz - sx * cy * sz;
            result.z = cx * cy * sz + sx * sy * cz;
            result.w = cx * cy * cz - sx * sy * sz;
            return result;
        }

        [[nodiscard]] static Math::float3 quat_to_euler(
            Math::float4 a_quat) noexcept
        {
            const float sinRollCosPitch =
                2.0f * (a_quat.w * a_quat.x + a_quat.y * a_quat.z);
            const float cosRollCosPitch =
                1.0f - 2.0f * (a_quat.x * a_quat.x + a_quat.y * a_quat.y);
            const float roll = std::atan2(sinRollCosPitch, cosRollCosPitch);

            const float sinPitch =
                2.0f * (a_quat.w * a_quat.y - a_quat.z * a_quat.x);
            const float pitch =
                std::abs(sinPitch) >= 1.0f
                ? std::copysign(Math::k_pi * 0.5f, sinPitch)
                : std::asin(sinPitch);

            const float sinYawCosPitch =
                2.0f * (a_quat.w * a_quat.z + a_quat.x * a_quat.y);
            const float cosYawCosPitch =
                1.0f - 2.0f * (a_quat.y * a_quat.y + a_quat.z * a_quat.z);
            const float yaw = std::atan2(sinYawCosPitch, cosYawCosPitch);

            return Math::float3(roll, pitch, yaw);
        }

    private:
        Physics::IPhysicsSystem* m_physicsSystem = nullptr;
        Phase m_phase = Phase::BeforeStep;
    };
}
