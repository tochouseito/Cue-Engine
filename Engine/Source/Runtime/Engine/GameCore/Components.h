#pragma once

// === Math includes ===
#include <CueMath.h>

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include "GameCoreTypes.h"

// === C++ includes ===
#include <limits>

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

    struct RenderObjectComponent : public IComponentTag
    {
        RenderObjectComponent() = default;
        RenderObjectComponent(const RenderObjectComponent&) = default;
        RenderObjectComponent& operator=(const RenderObjectComponent&) = default;
        RenderObjectComponent(RenderObjectComponent&&) = default;
        RenderObjectComponent& operator=(RenderObjectComponent&&) = default;
        uint32_t objectId = 0;
        uint32_t meshId = 0;
        uint32_t transformId = 0;
        bool visible = true;
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
        uint32_t materialId = 0; // マテリアルアセットの識別子
        bool isEnabled = true;
    };
}
