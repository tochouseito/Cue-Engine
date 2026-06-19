#pragma once

/// ************************************************************************************
/// エンジンのコアコンポーネント
/// ************************************************************************************

// === Math includes ===
#include <CueMath.h>

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include "GameCoreTypes.h"

// === C++ includes ===
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace Cue::GameCore
{
    // GameWorld が全 GameObject に共通で持たせる基本情報
    struct BaseComponent final : public ECS::IComponentTag
    {
        // GameWorld 内で一意になるよう管理される表示名
        std::string name{};
        // 検索用の分類ラベル
        std::string tag{ "Default" };
        // 所属 Scene永続 Object の場合は無効 SceneId を持つ
        SceneId owningSceneId = k_invalidSceneId;
        // 親子関係を表す親 Entity
        EntityId parent = k_invalidEntityId;
        // 自身のアクティブ状態
        bool isActiveSelf = true;
        // Scene アンロード時に削除せず残すかどうか
        bool isPersistent = false;
    };
}

namespace Cue::ECS
{
    inline constexpr uint32_t k_invalidMeshId =
        (std::numeric_limits<uint32_t>::max)();
    inline constexpr uint32_t k_invalidRenderableId =
        (std::numeric_limits<uint32_t>::max)();

    // --- Transform components ---

    /// @brief 描画 Object と Transform のランタイム登録 ID を保持する
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

    /// @brief Entity のローカル Transform を表す
    struct TransformComponent : public IComponentTag
    {
        TransformComponent() = default;
        TransformComponent(const TransformComponent&) = default;
        TransformComponent& operator=(const TransformComponent&) = default;
        TransformComponent(TransformComponent&&) = default;
        TransformComponent& operator=(TransformComponent&&) = default;
        Math::Quaternion rotation = Math::Quaternion::identity();
        Math::float3 position = Math::float3::zero();
        Math::float3 scale = Math::float3(1.0f, 1.0f, 1.0f);
    };

    /// @brief 親子階層を解決したワールド Transform を保持する
    struct WorldTransformComponent : public IComponentTag
    {
        WorldTransformComponent() = default;
        WorldTransformComponent(const WorldTransformComponent&) = default;
        WorldTransformComponent& operator=(const WorldTransformComponent&) =
            default;
        WorldTransformComponent(WorldTransformComponent&&) = default;
        WorldTransformComponent& operator=(WorldTransformComponent&&) =
            default;
        Math::Quaternion rotation = Math::Quaternion::identity();
        Math::float3 position = Math::float3::zero();
        Math::float3 scale = Math::float3(1.0f, 1.0f, 1.0f);
    };
}
