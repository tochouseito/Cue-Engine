#pragma once

// === Math includes ===
#include <CueMath.h>

// === ECS includes ===
#include <ECSManager.h>

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
