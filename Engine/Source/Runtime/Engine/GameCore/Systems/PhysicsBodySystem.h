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
        explicit PhysicsBodySystem(Physics::IPhysicsSystem* a_physicsSystem,
            AssetManager* a_assetManager)
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
            , m_assetManager(a_assetManager)
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
            Result shapeResult =
                build_shape_desc(a_transform, a_collider, desc.shape);
            if (!shapeResult)
            {
                CUE_ASSERTF(false, "Physics shape creation failed: %s",
                    shapeResult.message.data());
                return;
            }
            desc.shape.radius = (std::max)(a_collider.radius, 0.001f);
            desc.shape.halfHeight = (std::max)(a_collider.halfHeight, 0.001f);
            desc.position = a_transform.position + a_collider.offset;
            desc.rotation = to_float4(a_transform.rotation);
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

        [[nodiscard]] Result build_shape_desc(
            const TransformComponent& a_transform,
            const ColliderComponent& a_collider,
            Physics::ShapeDesc& a_outDesc) const
        {
            a_outDesc = {};
            a_outDesc.type = a_collider.type;
            a_outDesc.halfExtent = sanitize_half_extent(a_collider.halfExtent);
            a_outDesc.radius = (std::max)(a_collider.radius, 0.001f);
            a_outDesc.halfHeight = (std::max)(a_collider.halfHeight, 0.001f);

            if (a_collider.type != Physics::ShapeType::Mesh)
            {
                return Result::ok();
            }
            if (m_assetManager == nullptr || a_collider.meshModelName.empty())
            {
                return Result::fail(Code::InvalidState, Severity::Error,
                    "Mesh collider requires a model name and AssetManager.");
            }

            ModelHandle modelHandle{};
            Result result = m_assetManager->get_model(
                a_collider.meshModelName, modelHandle);
            if (!result)
            {
                return result;
            }

            Core::Native::ModelData modelData{};
            result = m_assetManager->get_model(modelHandle, modelData);
            if (!result)
            {
                return result;
            }

            for (const Core::Native::MeshData& mesh : modelData.meshes)
            {
                const uint32_t vertexBase =
                    static_cast<uint32_t>(a_outDesc.vertices.size());
                a_outDesc.vertices.reserve(
                    a_outDesc.vertices.size() + mesh.positions.size());
                for (const Math::float4& position : mesh.positions)
                {
                    a_outDesc.vertices.push_back(
                        Math::float3(
                            position.x * a_transform.scale.x,
                            position.y * a_transform.scale.y,
                            position.z * a_transform.scale.z));
                }

                a_outDesc.indices.reserve(
                    a_outDesc.indices.size() + mesh.indices.size());
                for (uint32_t index : mesh.indices)
                {
                    if (index >= mesh.positions.size())
                    {
                        return Result::fail(
                            Code::InvalidArgument,
                            Severity::Error,
                            "Mesh collider index is out of range.");
                    }
                    a_outDesc.indices.push_back(vertexBase + index);
                }
            }

            if (a_outDesc.vertices.empty() || a_outDesc.indices.empty())
            {
                return Result::fail(Code::InvalidArgument, Severity::Error,
                    "Mesh collider model has no geometry.");
            }

            return Result::ok();
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
            transform.rotation = to_float4(a_transform.rotation);
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
            a_transform.rotation = to_quaternion(transform.rotation);
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

        [[nodiscard]] static Math::float4 to_float4(
            Math::Quaternion a_rotation) noexcept
        {
            const Math::Quaternion rotation =
                Math::Quaternion::normalize(a_rotation);
            return Math::float4(
                rotation.x,
                rotation.y,
                rotation.z,
                rotation.w);
        }

        [[nodiscard]] static Math::Quaternion to_quaternion(
            Math::float4 a_quat) noexcept
        {
            return Math::Quaternion::normalize(Math::Quaternion(
                a_quat.x,
                a_quat.y,
                a_quat.z,
                a_quat.w));
        }

    private:
        Physics::IPhysicsSystem* m_physicsSystem = nullptr;
        AssetManager* m_assetManager = nullptr;
        Phase m_phase = Phase::BeforeStep;
    };
}
