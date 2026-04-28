#pragma once

// === Base includes ===
#include <Result.h>

// === Core includes ===
#include <Native/Handle.h>

// === Math includes ===
#include <CueMath.h>

// === C++ includes ===
#include <cstdint>

namespace Cue::Physics
{
    struct RigidBodyTag {};
    struct ShapeTag {};

    using RigidBodyHandle = Core::Handle<RigidBodyTag>;
    using ShapeHandle = Core::Handle<ShapeTag>;

    enum class MotionType : uint8_t
    {
        Static,
        Kinematic,
        Dynamic
    };

    enum class ShapeType : uint8_t
    {
        Box,
        Sphere,
        Capsule
    };

    enum class BodyActivation : uint8_t
    {
        Activate,
        DontActivate
    };

    struct PhysicsWorldDesc final
    {
        uint32_t maxBodyCount = 65536;
        uint32_t bodyMutexCount = 0;
        uint32_t maxBodyPairCount = 65536;
        uint32_t maxContactConstraintCount = 10240;
        uint32_t tempAllocatorSizeInBytes = 10u * 1024u * 1024u;
        uint32_t workerThreadCount = 0;
        Math::float3 gravity = Math::float3(0.0f, -9.80665f, 0.0f);
    };

    struct PhysicsStepDesc final
    {
        float deltaTime = 0.0f;
        uint32_t collisionStepCount = 1;
    };

    struct ShapeDesc final
    {
        ShapeType type = ShapeType::Box;
        Math::float3 halfExtent = Math::float3(0.5f, 0.5f, 0.5f);
        float radius = 0.5f;
        float halfHeight = 0.5f;
    };

    struct RigidBodyDesc final
    {
        ShapeDesc shape{};
        Math::float3 position = Math::float3::zero();
        Math::float4 rotation = Math::float4(0.0f, 0.0f, 0.0f, 1.0f);
        Math::float3 linearVelocity = Math::float3::zero();
        Math::float3 angularVelocity = Math::float3::zero();
        MotionType motion = MotionType::Static;
        BodyActivation activation = BodyActivation::Activate;
        float mass = 1.0f;
        float linearDamping = 0.05f;
        float angularDamping = 0.05f;
        float gravityFactor = 1.0f;
        float friction = 0.2f;
        float restitution = 0.0f;
        uint16_t collisionLayer = 0;
        bool isSensor = false;
    };

    struct BodyTransform final
    {
        Math::float3 position = Math::float3::zero();
        Math::float4 rotation = Math::float4(0.0f, 0.0f, 0.0f, 1.0f);
    };

    struct RaycastDesc final
    {
        Math::float3 origin = Math::float3::zero();
        Math::float3 direction = Math::float3(0.0f, -1.0f, 0.0f);
        float distance = 1000.0f;
    };

    struct RaycastHit final
    {
        RigidBodyHandle body{};
        Math::float3 position = Math::float3::zero();
        Math::float3 normal = Math::float3::zero();
        float distance = 0.0f;
    };

    /// @brief 物理シミュレーション backend の共通インターフェースです。
    class IPhysicsSystem
    {
    public:
        IPhysicsSystem() = default;
        IPhysicsSystem(const IPhysicsSystem&) = delete;
        IPhysicsSystem& operator=(const IPhysicsSystem&) = delete;
        IPhysicsSystem(IPhysicsSystem&&) = delete;
        IPhysicsSystem& operator=(IPhysicsSystem&&) = delete;
        virtual ~IPhysicsSystem() = default;

        /// @brief 物理 world を初期化します。
        virtual Result initialize(const PhysicsWorldDesc& a_desc) = 0;

        /// @brief 物理 world を終了します。
        virtual void shutdown() noexcept = 0;

        /// @brief 物理 world を指定時間だけ進めます。
        virtual Result step(const PhysicsStepDesc& a_desc) = 0;

        /// @brief 剛体を作成します。
        virtual Result create_body(
            const RigidBodyDesc& a_desc,
            RigidBodyHandle& a_outBody) = 0;

        /// @brief 剛体を破棄します。
        virtual Result destroy_body(RigidBodyHandle a_body) = 0;

        /// @brief 剛体の transform を設定します。
        virtual Result set_body_transform(
            RigidBodyHandle a_body,
            const BodyTransform& a_transform,
            BodyActivation a_activation) = 0;

        /// @brief 剛体の transform を取得します。
        virtual Result get_body_transform(
            RigidBodyHandle a_body,
            BodyTransform& a_outTransform) const = 0;

        /// @brief 最近傍の raycast 結果を取得します。
        virtual Result raycast(
            const RaycastDesc& a_desc,
            RaycastHit& a_outHit) const = 0;
    };
}
