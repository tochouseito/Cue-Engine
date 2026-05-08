#pragma once

// === Math includes ===
#include <CueMath.h>

// === ECS includes ===
#include <ECSManager.h>

// === Audio includes ===
#include <Audio.h>

// === Physics includes ===
#include <Physics.h>

// === Asset includes ===
#include <Asset/AssetManager.h>

// === Engine includes ===
#include "GameCoreTypes.h"

// === C++ includes ===
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace Cue::GameCore
{
    // GameWorld が全 GameObject に共通で持たせる基本情報。
    struct BaseComponent final : public ECS::IComponentTag
    {
        // GameWorld 内で一意になるよう管理される表示名。
        std::string name{};
        // 検索用の分類ラベル。
        std::string tag{ "Default" };
        // 所属 Scene。永続 Object の場合は無効 SceneId を持つ。
        SceneId owningSceneId = k_invalidSceneId;
        // 親子関係を表す親 Entity。
        EntityId parent = k_invalidEntityId;
        // 自身のアクティブ状態。
        bool isActiveSelf = true;
        // Scene アンロード時に削除せず残すかどうか。
        bool isPersistent = false;
    };
}

namespace Cue::ECS
{
    inline constexpr uint32_t k_invalidMeshId =
        (std::numeric_limits<uint32_t>::max)();
    inline constexpr uint32_t k_invalidRenderableId =
        (std::numeric_limits<uint32_t>::max)();

    struct RenderableInfoComponent : public IComponentTag
    {
        RenderableInfoComponent() = default;
        RenderableInfoComponent(const RenderableInfoComponent&) = default;
        RenderableInfoComponent& operator=(const RenderableInfoComponent&) =
            default;
        RenderableInfoComponent(RenderableInfoComponent&&) = default;
        RenderableInfoComponent& operator=(RenderableInfoComponent&&) = default;
        uint32_t objectId = k_invalidRenderableId;
        uint32_t transformId = k_invalidRenderableId;
    };

    struct TransformComponent : public IComponentTag
    {
        TransformComponent() = default;
        TransformComponent(const TransformComponent&) = default;
        TransformComponent& operator=(const TransformComponent&) = default;
        TransformComponent(TransformComponent&&) = default;
        TransformComponent& operator=(TransformComponent&&) = default;
        Math::float3 position = Math::float3::zero();
        Math::float3 rotation = Math::float3::zero();
        Math::float3 scale = Math::float3(1.0f, 1.0f, 1.0f);
    };

    struct CameraComponent : public IComponentTag
    {
        CameraComponent() = default;
        CameraComponent(const CameraComponent&) = default;
        CameraComponent& operator=(const CameraComponent&) = default;
        CameraComponent(CameraComponent&&) = default;
        CameraComponent& operator=(CameraComponent&&) = default;
        bool isMain = false; // 描画に使うメインカメラか
        float fovY = 60.0f; // 垂直視野角（度）
        float aspectRatio = 16.0f / 9.0f; // アスペクト比
        float nearZ = 0.1f; // ニアクリップ距離
        float farZ = 1000.0f; // ファークリップ距離
    };

    struct MeshFilterComponent : public IComponentTag
    {
        MeshFilterComponent() = default;
        MeshFilterComponent(const MeshFilterComponent&) = default;
        MeshFilterComponent& operator=(const MeshFilterComponent&) = default;
        MeshFilterComponent(MeshFilterComponent&&) = default;
        MeshFilterComponent& operator=(MeshFilterComponent&&) = default;
        std::string modelName{};
        uint32_t meshId = k_invalidMeshId; // StaticMeshPool に登録されたメッシュ ID
    };

    struct StaticMeshRendererComponent : public IComponentTag
    {
        StaticMeshRendererComponent() = default;
        StaticMeshRendererComponent(const StaticMeshRendererComponent&) = default;
        StaticMeshRendererComponent& operator=(const StaticMeshRendererComponent&) = default;
        StaticMeshRendererComponent(StaticMeshRendererComponent&&) = default;
        StaticMeshRendererComponent& operator=(StaticMeshRendererComponent&&) = default;
        MaterialHandle materialHandle{}; // マテリアルアセットへの参照
        bool visible = true;
    };

    struct SpriteRendererComponent : public IComponentTag
    {
        SpriteRendererComponent() = default;
        SpriteRendererComponent(const SpriteRendererComponent&) = default;
        SpriteRendererComponent& operator=(const SpriteRendererComponent&) = default;
        SpriteRendererComponent(SpriteRendererComponent&&) = default;
        SpriteRendererComponent& operator=(SpriteRendererComponent&&) = default;
        MaterialHandle materialHandle{};
        Math::float4 color = Math::float4(1.0f, 1.0f, 1.0f, 1.0f);
        Math::float4 uvRect = Math::float4(0.0f, 0.0f, 1.0f, 1.0f);
        Math::float2 size = Math::float2(64.0f, 64.0f);
        Math::float2 pivot = Math::float2(0.5f, 0.5f);
        int32_t layer = 0;
        uint32_t order = 0;
        bool isVisible = true;
    };

    struct AudioSourceComponent : public IComponentTag
    {
        AudioSourceComponent() = default;
        AudioSourceComponent(const AudioSourceComponent&) = default;
        AudioSourceComponent& operator=(const AudioSourceComponent&) = default;
        AudioSourceComponent(AudioSourceComponent&&) = default;
        AudioSourceComponent& operator=(AudioSourceComponent&&) = default;
        std::string fileName{};
        float spatialBlend = 0.0f;
        float volume = 1.0f;
        Audio::AudioSourceHandle sourceHandle{};
        bool playOnStart = false;
        bool isPlaying = false;
        bool playRequested = false;
        bool stopRequested = false;
        bool hasStarted = false;
        bool loop = false;
    };

    struct RigidBodyComponent : public IComponentTag
    {
        RigidBodyComponent() = default;
        RigidBodyComponent(const RigidBodyComponent&) = default;
        RigidBodyComponent& operator=(const RigidBodyComponent&) = default;
        RigidBodyComponent(RigidBodyComponent&&) = default;
        RigidBodyComponent& operator=(RigidBodyComponent&&) = default;
        Physics::RigidBodyHandle body{};
        Physics::MotionType motion = Physics::MotionType::Dynamic;
        Math::float3 linearVelocity = Math::float3::zero();
        Math::float3 angularVelocity = Math::float3::zero();
        float mass = 1.0f;
        float linearDamping = 0.05f;
        float angularDamping = 0.05f;
        bool useGravity = true;
        bool isCreated = false;
    };

    struct ColliderComponent : public IComponentTag
    {
        ColliderComponent() = default;
        ColliderComponent(const ColliderComponent&) = default;
        ColliderComponent& operator=(const ColliderComponent&) = default;
        ColliderComponent(ColliderComponent&&) = default;
        ColliderComponent& operator=(ColliderComponent&&) = default;
        Physics::ShapeType type = Physics::ShapeType::Box;
        std::string meshModelName{};
        Math::float3 offset = Math::float3::zero();
        Math::float3 halfExtent = Math::float3(0.5f, 0.5f, 0.5f);
        float radius = 0.5f;
        float halfHeight = 0.5f;
        float friction = 0.2f;
        float restitution = 0.0f;
        uint16_t layer = 0;
        uint16_t mask = 0xFFFFu;
        bool isTrigger = false;
    };

    struct CharacterControllerComponent : public IComponentTag
    {
        CharacterControllerComponent() = default;
        CharacterControllerComponent(const CharacterControllerComponent&) = default;
        CharacterControllerComponent& operator=(
            const CharacterControllerComponent&) = default;
        CharacterControllerComponent(CharacterControllerComponent&&) = default;
        CharacterControllerComponent& operator=(
            CharacterControllerComponent&&) = default;
        Math::float3 moveVelocity = Math::float3::zero();
        float verticalVelocity = 0.0f;
        float maxSpeed = 6.0f;
        float gravity = 9.80665f;
        float jumpSpeed = 5.0f;
        float groundCheckDistance = 0.12f;
        float skinWidth = 0.03f;
        bool isGrounded = false;
        bool jumpRequested = false;
    };

    struct FirstPersonCameraControllerComponent : public IComponentTag
    {
        GameCore::EntityId targetEntity = GameCore::k_invalidEntityId;
        Math::float3 offset = Math::float3(0.0f, 1.65f, 0.0f);
        float yaw = 0.0f;
        float pitch = 0.0f;
        float mouseSensitivity = 0.0025f;
        float minPitch = -1.45f;
        float maxPitch = 1.45f;
        float fovY = 60.0f;
        bool isEnabled = true;
        bool rotatesTargetYaw = true;
        bool followsTarget = true;
    };

    enum class LightType : uint8_t
    {
        Directional,
    };

    struct LightComponent : public IComponentTag
    {
        Math::float3 color = Math::float3(1.0f, 0.96f, 0.88f);
        Math::float3 groundAmbient = Math::float3(0.08f, 0.09f, 0.11f);
        LightType type = LightType::Directional;
        float intensity = 1.0f;
        float ambient = 0.18f;
        bool isEnabled = true;
    };

    struct TriggerVolumeComponent : public IComponentTag
    {
        std::vector<GameCore::EntityId> overlappingEntities{};
        std::vector<GameCore::EntityId> enteredEntities{};
        std::vector<GameCore::EntityId> exitedEntities{};
        bool includeTriggers = false;
    };

    struct InteractableComponent : public IComponentTag
    {
        std::string displayName{};
        float maxDistance = 3.0f;
        float holdDuration = 0.0f;
        bool isEnabled = true;
    };

    enum class DemoEnemyState : uint8_t
    {
        Idle,
        Patrol,
        MoveToTarget,
        ChasePlayer,
    };

    struct DemoEnemyComponent : public IComponentTag
    {
        GameCore::EntityId targetEntity = GameCore::k_invalidEntityId;
        std::vector<Math::float3> patrolPoints{};
        Math::float3 requestedDestination = Math::float3::zero();
        DemoEnemyState state = DemoEnemyState::Idle;
        uint32_t patrolIndex = 0;
        float chaseDistance = 8.0f;
        float stopDistance = 1.2f;
        bool hasRequestedDestination = false;
        bool isEnabled = true;
    };

    enum class ScriptFieldType : uint8_t
    {
        Float,
        Int32,
        Bool,
        EntityRef,
        ClassRef,
    };

    enum class ScriptFieldReferenceRole : uint8_t
    {
        None,
        ScriptReferenceEntity,
        ScriptReferenceClass,
    };

    enum class ScriptFieldFlags : uint32_t
    {
        None = 0,
        EditAnywhere = 1u << 0,
        Serialize = 1u << 1,
        ReadOnly = 1u << 2,
    };

    [[nodiscard]] inline constexpr ScriptFieldFlags operator|(
        ScriptFieldFlags a_left,
        ScriptFieldFlags a_right) noexcept
    {
        return static_cast<ScriptFieldFlags>(
            static_cast<uint32_t>(a_left) |
            static_cast<uint32_t>(a_right));
    }

    struct ScriptFieldValue final
    {
        ScriptFieldValue() = default;
        ScriptFieldValue(const ScriptFieldValue&) = default;
        ScriptFieldValue& operator=(const ScriptFieldValue&) = default;
        ScriptFieldValue(ScriptFieldValue&&) = default;
        ScriptFieldValue& operator=(ScriptFieldValue&&) = default;
        std::string name{};
        ScriptFieldType type = ScriptFieldType::Float;
        float floatValue = 0.0f;
        int32_t intValue = 0;
        bool boolValue = false;
        GameCore::EntityId entityValue = GameCore::k_invalidEntityId;
        std::string classValue{};
        std::string groupName{};
        ScriptFieldReferenceRole referenceRole =
            ScriptFieldReferenceRole::None;
        ScriptFieldFlags flags = ScriptFieldFlags::EditAnywhere |
            ScriptFieldFlags::Serialize;
    };

    struct ScriptComponent : public IComponentTag
    {
        ScriptComponent() = default;
        ScriptComponent(const ScriptComponent&) = default;
        ScriptComponent& operator=(const ScriptComponent&) = default;
        ScriptComponent(ScriptComponent&&) = default;
        ScriptComponent& operator=(ScriptComponent&&) = default;
        std::string className{};
        bool isEnabled = true;
        std::vector<ScriptFieldValue> serializedFieldValues{};
        std::vector<ScriptFieldValue> transientFieldValues{};
    };
}
