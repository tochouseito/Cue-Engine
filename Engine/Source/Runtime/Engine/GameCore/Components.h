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
        std::string tag{"Default"};
        // 所属 Scene。永続 Object の場合は無効 SceneId を持つ
        SceneId owningSceneId = k_invalidSceneId;
        // 親子関係を表す親 Entity
        EntityId parent = k_invalidEntityId;
        // 自身のアクティブ状態
        bool isActiveSelf = true;
        // Scene アンロード時に削除せず残すかどうか
        bool isPersistent = false;
    };
} // namespace Cue::GameCore

namespace Cue::ECS
{
    // MeshPool に存在しない mesh を表す sentinel ID
    inline constexpr uint32_t k_invalidMeshId = (std::numeric_limits<uint32_t>::max)();
    // DrawSystem にまだ登録されていない renderable/transform を表す sentinel ID
    inline constexpr uint32_t k_invalidRenderableId = (std::numeric_limits<uint32_t>::max)();
    // Material table に存在しない material を表す sentinel ID
    inline constexpr uint32_t k_invalidMaterialId = (std::numeric_limits<uint32_t>::max)();

    // --- Transform components ---

    /// @brief 描画 Object と Transform のランタイム登録 ID を保持する
    struct RenderableInfoComponent : public IComponentTag
    {
        RenderableInfoComponent();
        RenderableInfoComponent(const RenderableInfoComponent&);
        RenderableInfoComponent& operator=(const RenderableInfoComponent&);
        RenderableInfoComponent(RenderableInfoComponent&&);
        RenderableInfoComponent& operator=(RenderableInfoComponent&&);

        // DrawSystem 側で割り当てる renderable ID
        uint32_t objectId = k_invalidRenderableId;
        // DrawSystem 側で割り当てる transform ID
        uint32_t transformId = k_invalidRenderableId;
    };

    /// @brief Entity のローカル Transform を表す
    struct TransformComponent : public IComponentTag
    {
        TransformComponent();
        TransformComponent(const TransformComponent&);
        TransformComponent& operator=(const TransformComponent&);
        TransformComponent(TransformComponent&&);
        TransformComponent& operator=(TransformComponent&&);

        // Entity のローカル回転
        Math::Quaternion rotation = Math::Quaternion::identity();
        // Entity のローカル位置
        Math::float3 position = Math::float3::zero();
        // Entity のローカルスケール
        Math::float3 scale = Math::float3(1.0f, 1.0f, 1.0f);
    };

    /// @brief 親子階層を解決したワールド Transform を保持する
    struct WorldTransformComponent : public IComponentTag
    {
        WorldTransformComponent();
        WorldTransformComponent(const WorldTransformComponent&);
        WorldTransformComponent& operator=(const WorldTransformComponent&);
        WorldTransformComponent(WorldTransformComponent&&);
        WorldTransformComponent& operator=(WorldTransformComponent&&);

        // Entity のワールド回転
        Math::Quaternion rotation = Math::Quaternion::identity();
        // Entity のワールド位置
        Math::float3 position = Math::float3::zero();
        // Entity のワールドスケール
        Math::float3 scale = Math::float3(1.0f, 1.0f, 1.0f);
    };

    /// @brief 描画に使うカメラ設定
    struct CameraComponent : public IComponentTag
    {
        CameraComponent();
        CameraComponent(const CameraComponent&);
        CameraComponent& operator=(const CameraComponent&);
        CameraComponent(CameraComponent&&);
        CameraComponent& operator=(CameraComponent&&);

        // main camera として DrawSystem の ViewProjectionBuffer に採用するか
        bool isMain = true;
        // 縦方向 FOV。CueEngine と同じく度数法で保持する
        float fovY = 60.0f;
        // 0 以下の場合は renderWidth / renderHeight から算出する
        float aspectRatio = 0.0f;
        // projection の depth range
        float nearZ = 0.1f;
        float farZ = 1000.0f;
    };

    /// @brief StaticMeshPool に登録された Mesh を参照する
    struct MeshFilterComponent : public IComponentTag
    {
        MeshFilterComponent();
        MeshFilterComponent(const MeshFilterComponent&);
        MeshFilterComponent& operator=(const MeshFilterComponent&);
        MeshFilterComponent(MeshFilterComponent&&);
        MeshFilterComponent& operator=(MeshFilterComponent&&);

        // Asset/Debug 用のモデル名。最小構成では空でもよい
        std::string modelName{};
        // StaticMeshPool 上の mesh ID
        uint32_t meshId = k_invalidMeshId;
    };

    enum class ShadowCasterMode : uint8_t
    {
        // 通常の片面 shadow caster
        Solid = 0,
        // 両面描画が必要な shadow caster
        TwoSided = 1,
    };

    enum class RenderQueue : uint8_t
    {
        // 不透明 object として描画する
        Opaque = 0,
        // 透明 object として描画する
        Transparent = 1,
        // material/renderer 側の既定規則で決定する
        Auto = 2,
    };

    enum MaterialPropertyOverride : uint32_t
    {
        // color を MaterialPropertyBlock で上書きする
        MaterialPropertyOverrideColor = 1u << 0,
        // shininess を MaterialPropertyBlock で上書きする
        MaterialPropertyOverrideShininess = 1u << 1,
        // reflection skybox 使用有無を MaterialPropertyBlock で上書きする
        MaterialPropertyOverrideReflectionSkybox = 1u << 2,
    };

    struct MaterialPropertyBlock final
    {
        // material の base color override
        Math::float4 color = Math::float4(1.0f, 1.0f, 1.0f, 1.0f);
        // specular 計算などで使う光沢度
        float shininess = 32.0f;
        // どのプロパティを上書きするかを示す bit mask
        uint32_t overrideMask = 0u;
        // reflection skybox を使用するか
        bool usesReflectionSkybox = false;
    };

    /// @brief IndirectCommand batching に渡す StaticMesh 描画設定
    struct StaticMeshRendererComponent : public IComponentTag
    {
        StaticMeshRendererComponent();
        StaticMeshRendererComponent(const StaticMeshRendererComponent&);
        StaticMeshRendererComponent& operator=(const StaticMeshRendererComponent&);
        StaticMeshRendererComponent(StaticMeshRendererComponent&&);
        StaticMeshRendererComponent& operator=(StaticMeshRendererComponent&&);

        // Material table 上の material ID
        uint32_t materialId = k_invalidMaterialId;
        // Entity 単位の material override
        MaterialPropertyBlock propertyBlock{};
        // 不透明/透明などの描画キュー
        RenderQueue renderQueue = RenderQueue::Auto;
        // shadow map pass での描画方法
        ShadowCasterMode shadowCasterMode = ShadowCasterMode::Solid;
        // 通常描画に含めるか
        bool visible = true;
        // shadow caster として扱うか
        bool castsShadow = true;
        // shadow receiver として扱うか
        bool receivesShadow = true;
    };
} // namespace Cue::ECS
