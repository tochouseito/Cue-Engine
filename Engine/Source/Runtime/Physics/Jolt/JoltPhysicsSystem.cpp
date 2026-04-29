// === Physics includes ===
#include "JoltPhysicsSystem.h"

// === Jolt includes ===
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

// === C++ includes ===
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <limits>
#include <thread>

namespace
{
    namespace Layers
    {
        static constexpr JPH::ObjectLayer k_nonMoving = 0;
        static constexpr JPH::ObjectLayer k_moving = 1;
        static constexpr JPH::ObjectLayer k_count = 2;
    }

    namespace BroadPhaseLayers
    {
        static constexpr JPH::BroadPhaseLayer k_nonMoving(0);
        static constexpr JPH::BroadPhaseLayer k_moving(1);
        static constexpr uint32_t k_count = 2;
    }

    class BroadPhaseLayerInterface final
        : public JPH::BroadPhaseLayerInterface
    {
    public:
        JPH::uint GetNumBroadPhaseLayers() const override
        {
            return BroadPhaseLayers::k_count;
        }

        JPH::BroadPhaseLayer GetBroadPhaseLayer(
            JPH::ObjectLayer a_layer) const override
        {
            switch (a_layer)
            {
            case Layers::k_nonMoving:
                return BroadPhaseLayers::k_nonMoving;
            case Layers::k_moving:
                return BroadPhaseLayers::k_moving;
            default:
                return BroadPhaseLayers::k_moving;
            }
        }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        const char* GetBroadPhaseLayerName(
            JPH::BroadPhaseLayer a_layer) const override
        {
            switch (a_layer.GetValue())
            {
            case 0:
                return "NonMoving";
            case 1:
                return "Moving";
            default:
                return "Unknown";
            }
        }
#endif
    };

    class ObjectVsBroadPhaseLayerFilter final
        : public JPH::ObjectVsBroadPhaseLayerFilter
    {
    public:
        bool ShouldCollide(
            JPH::ObjectLayer a_layer,
            JPH::BroadPhaseLayer a_broadPhaseLayer) const override
        {
            switch (a_layer)
            {
            case Layers::k_nonMoving:
                return a_broadPhaseLayer == BroadPhaseLayers::k_moving;
            case Layers::k_moving:
                return true;
            default:
                return false;
            }
        }
    };

    class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
    {
    public:
        bool ShouldCollide(
            JPH::ObjectLayer a_left,
            JPH::ObjectLayer a_right) const override
        {
            switch (a_left)
            {
            case Layers::k_nonMoving:
                return a_right == Layers::k_moving;
            case Layers::k_moving:
                return true;
            default:
                return false;
            }
        }
    };

    BroadPhaseLayerInterface g_broadPhaseLayerInterface{};
    ObjectVsBroadPhaseLayerFilter g_objectVsBroadPhaseLayerFilter{};
    ObjectLayerPairFilter g_objectLayerPairFilter{};
    uint32_t g_joltInstanceCount = 0;

    void trace_impl(const char* a_format, ...)
    {
        va_list args{};
        va_start(args, a_format);
        char buffer[1024]{};
        std::vsnprintf(buffer, sizeof(buffer), a_format, args);
        va_end(args);
        std::fputs(buffer, stdout);
        std::fputc('\n', stdout);
    }

#ifdef JPH_ENABLE_ASSERTS
    bool assert_failed_impl(
        const char* a_expression,
        const char* a_message,
        const char* a_file,
        JPH::uint a_line)
    {
        std::fprintf(stderr, "%s:%u: (%s) %s\n",
            a_file,
            a_line,
            a_expression,
            a_message != nullptr ? a_message : "");
        return true;
    }
#endif

    void initialize_jolt_global()
    {
        if (g_joltInstanceCount == 0)
        {
            JPH::RegisterDefaultAllocator();
            JPH::Trace = trace_impl;
#ifdef JPH_ENABLE_ASSERTS
            JPH::AssertFailed = assert_failed_impl;
#endif
            static JPH::Factory factory{};
            JPH::Factory::sInstance = &factory;
            JPH::RegisterTypes();
        }
        ++g_joltInstanceCount;
    }

    void shutdown_jolt_global() noexcept
    {
        if (g_joltInstanceCount == 0)
        {
            return;
        }

        --g_joltInstanceCount;
        if (g_joltInstanceCount == 0)
        {
            JPH::UnregisterTypes();
            JPH::Factory::sInstance = nullptr;
        }
    }

    [[nodiscard]] JPH::Vec3 to_vec3(Cue::Math::float3 a_value) noexcept
    {
        return JPH::Vec3(a_value.x, a_value.y, a_value.z);
    }

    [[nodiscard]] JPH::RVec3 to_rvec3(Cue::Math::float3 a_value) noexcept
    {
        return JPH::RVec3(a_value.x, a_value.y, a_value.z);
    }

    [[nodiscard]] JPH::Quat to_quat(Cue::Math::float4 a_value) noexcept
    {
        return JPH::Quat(a_value.x, a_value.y, a_value.z, a_value.w);
    }

    template <class Vec>
    [[nodiscard]] Cue::Math::float3 to_float3(const Vec& a_value) noexcept
    {
        return Cue::Math::float3(
            static_cast<float>(a_value.GetX()),
            static_cast<float>(a_value.GetY()),
            static_cast<float>(a_value.GetZ()));
    }

    [[nodiscard]] Cue::Math::float4 to_float4(JPH::QuatArg a_value) noexcept
    {
        return Cue::Math::float4(
            a_value.GetX(), a_value.GetY(), a_value.GetZ(), a_value.GetW());
    }

    [[nodiscard]] JPH::EMotionType to_motion_type(
        Cue::Physics::MotionType a_motion) noexcept
    {
        switch (a_motion)
        {
        case Cue::Physics::MotionType::Static:
            return JPH::EMotionType::Static;
        case Cue::Physics::MotionType::Kinematic:
            return JPH::EMotionType::Kinematic;
        case Cue::Physics::MotionType::Dynamic:
            return JPH::EMotionType::Dynamic;
        default:
            return JPH::EMotionType::Static;
        }
    }

    [[nodiscard]] JPH::EActivation to_activation(
        Cue::Physics::BodyActivation a_activation) noexcept
    {
        return a_activation == Cue::Physics::BodyActivation::Activate
            ? JPH::EActivation::Activate
            : JPH::EActivation::DontActivate;
    }

    [[nodiscard]] JPH::ObjectLayer to_layer(
        Cue::Physics::MotionType a_motion,
        uint16_t a_layer) noexcept
    {
        a_layer;

        return a_motion == Cue::Physics::MotionType::Static
            ? Layers::k_nonMoving
            : Layers::k_moving;
    }

    [[nodiscard]] Cue::Result create_shape(
        const Cue::Physics::ShapeDesc& a_desc,
        JPH::ShapeRefC& a_outShape)
    {
        a_outShape = nullptr;

        JPH::ShapeSettings::ShapeResult result{};
        switch (a_desc.type)
        {
        case Cue::Physics::ShapeType::Box:
        {
            if (a_desc.halfExtent.x <= 0.0f ||
                a_desc.halfExtent.y <= 0.0f ||
                a_desc.halfExtent.z <= 0.0f)
            {
                return Cue::Result::fail(Cue::Code::InvalidArgument, Cue::Severity::Error,
                    "Box half extent must be greater than zero.");
            }
            JPH::BoxShapeSettings settings(to_vec3(a_desc.halfExtent));
            result = settings.Create();
            break;
        }
        case Cue::Physics::ShapeType::Sphere:
        {
            if (a_desc.radius <= 0.0f)
            {
                return Cue::Result::fail(Cue::Code::InvalidArgument, Cue::Severity::Error,
                    "Sphere radius must be greater than zero.");
            }
            JPH::SphereShapeSettings settings(a_desc.radius);
            result = settings.Create();
            break;
        }
        case Cue::Physics::ShapeType::Capsule:
        {
            if (a_desc.radius <= 0.0f || a_desc.halfHeight <= 0.0f)
            {
                return Cue::Result::fail(Cue::Code::InvalidArgument, Cue::Severity::Error,
                    "Capsule radius and half height must be greater than zero.");
            }
            JPH::CapsuleShapeSettings settings(a_desc.halfHeight, a_desc.radius);
            result = settings.Create();
            break;
        }
        default:
            return Cue::Result::fail(Cue::Code::Unsupported, Cue::Severity::Error,
                "Physics shape type is unsupported.");
        }

        if (result.HasError())
        {
            return Cue::Result::fail(Cue::Code::CreateFailed, Cue::Severity::Error,
                "Failed to create Jolt shape.");
        }

        a_outShape = result.Get();
        return Cue::Result::ok();
    }
}

namespace Cue::Physics::Jolt
{
    JoltPhysicsSystem::JoltPhysicsSystem() = default;

    JoltPhysicsSystem::~JoltPhysicsSystem()
    {
        shutdown();
    }

    Result JoltPhysicsSystem::initialize(const PhysicsWorldDesc& a_desc)
    {
        if (m_isInitialized)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Jolt physics system is already initialized.");
        }
        if (a_desc.maxBodyCount == 0 ||
            a_desc.maxBodyPairCount == 0 ||
            a_desc.maxContactConstraintCount == 0 ||
            a_desc.tempAllocatorSizeInBytes == 0)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Jolt physics world desc contains zero capacity.");
        }

        initialize_jolt_global();

        const uint32_t hardwareThreadCount = std::thread::hardware_concurrency();
        const uint32_t workerThreadCount =
            a_desc.workerThreadCount != 0
            ? a_desc.workerThreadCount
            : (hardwareThreadCount > 1 ? hardwareThreadCount - 1 : 1);

        m_tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(
            a_desc.tempAllocatorSizeInBytes);
        m_jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
            JPH::cMaxPhysicsJobs,
            JPH::cMaxPhysicsBarriers,
            static_cast<int>(workerThreadCount));
        m_physicsSystem = std::make_unique<JPH::PhysicsSystem>();

        const uint32_t bodyMutexCount =
            a_desc.bodyMutexCount != 0 ? a_desc.bodyMutexCount : 0;
        m_physicsSystem->Init(
            a_desc.maxBodyCount,
            bodyMutexCount,
            a_desc.maxBodyPairCount,
            a_desc.maxContactConstraintCount,
            g_broadPhaseLayerInterface,
            g_objectVsBroadPhaseLayerFilter,
            g_objectLayerPairFilter);
        m_physicsSystem->SetGravity(to_vec3(a_desc.gravity));

        m_isInitialized = true;
        return Result::ok();
    }

    void JoltPhysicsSystem::shutdown() noexcept
    {
        if (!m_isInitialized)
        {
            return;
        }

        if (m_physicsSystem != nullptr)
        {
            JPH::BodyInterface& bodyInterface =
                m_physicsSystem->GetBodyInterface();
            for (BodyRecord& record : m_bodyRecords)
            {
                if (!record.isAlive)
                {
                    continue;
                }

                const JPH::BodyID id(record.id);
                bodyInterface.RemoveBody(id);
                bodyInterface.DestroyBody(id);
                record = BodyRecord{};
            }
        }

        m_freeBodyIndices.clear();
        m_bodyRecords.clear();
        m_physicsSystem.reset();
        m_jobSystem.reset();
        m_tempAllocator.reset();
        m_isInitialized = false;
        shutdown_jolt_global();
    }

    Result JoltPhysicsSystem::step(const PhysicsStepDesc& a_desc)
    {
        if (!m_isInitialized || m_physicsSystem == nullptr ||
            m_tempAllocator == nullptr || m_jobSystem == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Jolt physics system is not initialized.");
        }
        if (a_desc.deltaTime <= 0.0f)
        {
            return Result::ok();
        }

        const int collisionStepCount =
            static_cast<int>(std::max(1u, a_desc.collisionStepCount));
        const JPH::EPhysicsUpdateError error = m_physicsSystem->Update(
            a_desc.deltaTime,
            collisionStepCount,
            m_tempAllocator.get(),
            m_jobSystem.get());
        if (error != JPH::EPhysicsUpdateError::None)
        {
            return Result::fail(Code::InternalError, Severity::Error,
                "Jolt physics update failed.");
        }

        return Result::ok();
    }

    Result JoltPhysicsSystem::create_body(
        const RigidBodyDesc& a_desc,
        RigidBodyHandle& a_outBody)
    {
        a_outBody = {};
        if (!m_isInitialized || m_physicsSystem == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Jolt physics system is not initialized.");
        }

        JPH::ShapeRefC shape{};
        Result result = create_shape(a_desc.shape, shape);
        if (!result)
        {
            return result;
        }

        JPH::BodyCreationSettings settings(
            shape.GetPtr(),
            to_rvec3(a_desc.position),
            to_quat(a_desc.rotation),
            to_motion_type(a_desc.motion),
            to_layer(a_desc.motion, a_desc.collisionLayer));
        settings.mFriction = a_desc.friction;
        settings.mRestitution = a_desc.restitution;
        settings.mLinearDamping = (std::max)(a_desc.linearDamping, 0.0f);
        settings.mAngularDamping = (std::max)(a_desc.angularDamping, 0.0f);
        settings.mGravityFactor = (std::max)(a_desc.gravityFactor, 0.0f);
        settings.mIsSensor = a_desc.isSensor;
        settings.mLinearVelocity = to_vec3(a_desc.linearVelocity);
        settings.mAngularVelocity = to_vec3(a_desc.angularVelocity);
        if (a_desc.motion == MotionType::Dynamic)
        {
            settings.mOverrideMassProperties =
                JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass =
                (std::max)(a_desc.mass, 0.001f);
        }

        JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
        JPH::Body* body = bodyInterface.CreateBody(settings);
        if (body == nullptr)
        {
            return Result::fail(Code::CreateFailed, Severity::Error,
                "Failed to create Jolt body.");
        }

        bodyInterface.AddBody(body->GetID(), to_activation(a_desc.activation));

        uint32_t index = 0;
        if (!m_freeBodyIndices.empty())
        {
            index = m_freeBodyIndices.back();
            m_freeBodyIndices.pop_back();
        }
        else
        {
            if (m_bodyRecords.size() >=
                static_cast<size_t>((std::numeric_limits<uint32_t>::max)()))
            {
                bodyInterface.RemoveBody(body->GetID());
                bodyInterface.DestroyBody(body->GetID());
                return Result::fail(Code::OutOfMemory, Severity::Fatal,
                    "Physics body handle capacity exceeded.");
            }
            index = static_cast<uint32_t>(m_bodyRecords.size());
            m_bodyRecords.push_back(BodyRecord{});
        }

        BodyRecord& record = m_bodyRecords[index];
        record.id = body->GetID().GetIndexAndSequenceNumber();
        record.isAlive = true;
        a_outBody = RigidBodyHandle{ index, record.generation };
        return Result::ok();
    }

    Result JoltPhysicsSystem::destroy_body(RigidBodyHandle a_body)
    {
        if (!m_isInitialized || m_physicsSystem == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Jolt physics system is not initialized.");
        }
        if (!is_alive(a_body))
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Physics body handle is invalid.");
        }

        const JPH::BodyID id = body_id(a_body);
        JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
        bodyInterface.RemoveBody(id);
        bodyInterface.DestroyBody(id);

        BodyRecord& record = m_bodyRecords[a_body.index];
        record.isAlive = false;
        ++record.generation;
        m_freeBodyIndices.push_back(a_body.index);
        return Result::ok();
    }

    Result JoltPhysicsSystem::set_body_transform(
        RigidBodyHandle a_body,
        const BodyTransform& a_transform,
        BodyActivation a_activation)
    {
        if (!m_isInitialized || m_physicsSystem == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Jolt physics system is not initialized.");
        }
        if (!is_alive(a_body))
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Physics body handle is invalid.");
        }

        JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
        bodyInterface.SetPositionAndRotationWhenChanged(
            body_id(a_body),
            to_rvec3(a_transform.position),
            to_quat(a_transform.rotation),
            to_activation(a_activation));
        return Result::ok();
    }

    Result JoltPhysicsSystem::get_body_transform(
        RigidBodyHandle a_body,
        BodyTransform& a_outTransform) const
    {
        a_outTransform = {};
        if (!m_isInitialized || m_physicsSystem == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Jolt physics system is not initialized.");
        }
        if (!is_alive(a_body))
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Physics body handle is invalid.");
        }

        JPH::RVec3 position{};
        JPH::Quat rotation{};
        m_physicsSystem->GetBodyInterface().GetPositionAndRotation(
            body_id(a_body), position, rotation);
        a_outTransform.position = to_float3(position);
        a_outTransform.rotation = to_float4(rotation);
        return Result::ok();
    }

    Result JoltPhysicsSystem::raycast(
        const RaycastDesc& a_desc,
        RaycastHit& a_outHit) const
    {
        a_outHit = {};
        if (!m_isInitialized || m_physicsSystem == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Jolt physics system is not initialized.");
        }
        if (a_desc.distance <= 0.0f || a_desc.direction.is_zero())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Physics raycast desc is invalid.");
        }

        Math::float3 direction = a_desc.direction;
        direction.normalize();
        const JPH::RRayCast ray(
            to_rvec3(a_desc.origin),
            to_vec3(direction) * a_desc.distance);

        JPH::RayCastResult hit{};
        const bool hasHit =
            m_physicsSystem->GetNarrowPhaseQuery().CastRay(ray, hit);
        if (!hasHit)
        {
            return Result::fail(Code::NotFound, Severity::Info,
                "Physics raycast did not hit.");
        }

        a_outHit.body = find_body_handle(hit.mBodyID);
        a_outHit.distance = hit.mFraction * a_desc.distance;
        a_outHit.position = to_float3(ray.GetPointOnRay(hit.mFraction));

        JPH::BodyLockRead lock(
            m_physicsSystem->GetBodyLockInterface(),
            hit.mBodyID);
        if (lock.Succeeded())
        {
            const JPH::Body& body = lock.GetBody();
            a_outHit.normal = to_float3(
                body.GetWorldSpaceSurfaceNormal(
                    hit.mSubShapeID2,
                    ray.GetPointOnRay(hit.mFraction)));
        }

        return Result::ok();
    }

    bool JoltPhysicsSystem::is_alive(RigidBodyHandle a_body) const noexcept
    {
        return a_body.valid() &&
            a_body.index < m_bodyRecords.size() &&
            m_bodyRecords[a_body.index].isAlive &&
            a_body.generation == m_bodyRecords[a_body.index].generation;
    }

    JPH::BodyID JoltPhysicsSystem::body_id(RigidBodyHandle a_body) const noexcept
    {
        const BodyRecord& record = m_bodyRecords[a_body.index];
        return JPH::BodyID(record.id);
    }

    RigidBodyHandle JoltPhysicsSystem::find_body_handle(
        JPH::BodyID a_bodyId) const noexcept
    {
        for (uint32_t index = 0;
             index < static_cast<uint32_t>(m_bodyRecords.size());
             ++index)
        {
            const BodyRecord& record = m_bodyRecords[index];
            if (!record.isAlive)
            {
                continue;
            }
            const JPH::BodyID id(record.id);
            if (id == a_bodyId)
            {
                return RigidBodyHandle{ index, record.generation };
            }
        }

        return {};
    }
}
