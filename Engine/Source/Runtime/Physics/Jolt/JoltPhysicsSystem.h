#pragma once

// === Physics includes ===
#include <Physics.h>

// === C++ includes ===
#include <memory>
#include <vector>

namespace JPH
{
    class BodyInterface;
    class BodyID;
    class JobSystemThreadPool;
    class PhysicsSystem;
    class TempAllocatorImpl;
}

namespace Cue::Physics::Jolt
{
    /// @brief Jolt Physics を利用する物理 backend です。
    class JoltPhysicsSystem final : public IPhysicsSystem
    {
    public:
        JoltPhysicsSystem();
        JoltPhysicsSystem(const JoltPhysicsSystem&) = delete;
        JoltPhysicsSystem& operator=(const JoltPhysicsSystem&) = delete;
        JoltPhysicsSystem(JoltPhysicsSystem&&) = delete;
        JoltPhysicsSystem& operator=(JoltPhysicsSystem&&) = delete;
        ~JoltPhysicsSystem() override;

        Result initialize(const PhysicsWorldDesc& a_desc) override;
        void shutdown() noexcept override;
        Result step(const PhysicsStepDesc& a_desc) override;
        Result create_body(
            const RigidBodyDesc& a_desc,
            RigidBodyHandle& a_outBody) override;
        Result destroy_body(RigidBodyHandle a_body) override;
        Result set_body_transform(
            RigidBodyHandle a_body,
            const BodyTransform& a_transform,
            BodyActivation a_activation) override;
        Result get_body_transform(
            RigidBodyHandle a_body,
            BodyTransform& a_outTransform) const override;
        Result set_linear_velocity(
            RigidBodyHandle a_body,
            Math::float3 a_velocity,
            BodyActivation a_activation) override;
        Result get_linear_velocity(
            RigidBodyHandle a_body,
            Math::float3& a_outVelocity) const override;
        Result add_force(
            RigidBodyHandle a_body,
            Math::float3 a_force,
            BodyActivation a_activation) override;
        Result add_impulse(
            RigidBodyHandle a_body,
            Math::float3 a_impulse,
            BodyActivation a_activation) override;
        Result raycast(
            const RaycastDesc& a_desc,
            RaycastHit& a_outHit) const override;

    private:
        struct BodyRecord final
        {
            uint32_t id = 0;
            uint32_t generation = 0;
            bool isAlive = false;
        };

        [[nodiscard]] bool is_alive(RigidBodyHandle a_body) const noexcept;
        [[nodiscard]] JPH::BodyID body_id(RigidBodyHandle a_body) const noexcept;
        [[nodiscard]] RigidBodyHandle find_body_handle(
            JPH::BodyID a_bodyId) const noexcept;

    private:
        std::unique_ptr<JPH::TempAllocatorImpl> m_tempAllocator = nullptr;
        std::unique_ptr<JPH::JobSystemThreadPool> m_jobSystem = nullptr;
        std::unique_ptr<JPH::PhysicsSystem> m_physicsSystem = nullptr;
        std::vector<BodyRecord> m_bodyRecords{};
        std::vector<uint32_t> m_freeBodyIndices{};
        bool m_isInitialized = false;
    };
}
