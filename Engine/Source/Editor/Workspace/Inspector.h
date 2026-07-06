#pragma once

/// **********************************************************************
/// 選択中 GameObject の Component 情報を表示、編集する Editor View
/// **********************************************************************

// === Runtime includes ===
#include <GameCore/GameCoreTypes.h>

// === Math includes ===
#include <CueMath.h>

// === C++ includes ===
#include <cstdint>

namespace Cue
{
    enum class ComponentKind : uint8_t;
}

namespace Cue::GameCore
{
    class GameObject;
    class GameWorld;
}

namespace Cue::Core::CQRS
{
    class Bridge;
}

namespace Cue::ECS
{
    struct CameraComponent;
    struct MeshFilterComponent;
    struct StaticMeshRendererComponent;
    struct TransformComponent;
}

namespace Cue::DrawSystem
{
    class MeshPool;
}

namespace Cue::Editor
{
    /// @brief EditorManager の選択状態を参照して GameObject の Component 情報を表示、編集する
    class Inspector final
    {
    public:
        /// @brief Editor 共有状態を参照して Inspector View を構築する
        /// @param a_commandBridge Component 編集コマンドの送信先
        /// @param a_gameWorld 表示対象の GameWorld
        /// @param a_meshPool MeshFilter に割り当てる mesh の一覧取得元
        /// @param a_selectedEntityId Editor 全体で共有する選択 Entity
        Inspector(Core::CQRS::Bridge* a_commandBridge,
                  GameCore::GameWorld* a_gameWorld,
                  DrawSystem::MeshPool* a_meshPool,
                  GameCore::EntityId* a_selectedEntityId) noexcept;
        ~Inspector() = default;

        /// @brief 表示対象の GameWorld を差し替える
        void set_game_world(GameCore::GameWorld* a_gameWorld) noexcept;

        /// @brief MeshFilter が参照する MeshPool を差し替える
        void set_mesh_pool(DrawSystem::MeshPool* a_meshPool) noexcept;

        /// @brief Inspector window を描画する
        void update();

    private:
        /// @brief 選択中 Entity の GameObject handle を探す
        [[nodiscard]] bool find_selected_object(GameCore::GameObject& a_outObject);

        /// @brief 選択中 GameObject に追加できる Component の menu を描画する
        void draw_add_component_menu(GameCore::GameObject& a_object);

        /// @brief Scene 所有や親子関係など Object の基礎情報を読み取り専用で表示する
        void draw_base_component(GameCore::GameObject& a_object);

        /// @brief local transform を編集し、GameWorld への反映は CQRS に委譲する
        void draw_transform_component(GameCore::GameObject& a_object);

        /// @brief 親子関係を解決した world transform を読み取り専用で表示する
        void draw_world_transform_component(GameCore::GameObject& a_object);

        /// @brief Camera の projection パラメータを有効範囲に制限して編集する
        void draw_camera_component(GameCore::GameObject& a_object);

        /// @brief MeshPool 上の名前付き mesh 参照を選択する
        void draw_mesh_filter_component(GameCore::GameObject& a_object);

        /// @brief material と描画制御値を編集し、描画抽出へ反映させる
        void draw_static_mesh_renderer_component(GameCore::GameObject& a_object);

        /// @brief GPU 描画用に割り当てられた ID を読み取り専用で表示する
        void draw_renderable_info_component(GameCore::GameObject& a_object);

        /// @brief TransformComponent の変更を CQRS 経由で要求する
        void submit_transform_component(
            GameCore::EntityId a_entityId,
            const ECS::TransformComponent& a_component);

        /// @brief Quaternion から Editor 表示用 Euler 角を同期する
        void sync_rotation_cache(
            GameCore::EntityId a_entityId,
            const ECS::TransformComponent& a_component) noexcept;

        /// @brief CameraComponent の変更を CQRS 経由で要求する
        void submit_camera_component(
            GameCore::EntityId a_entityId,
            const ECS::CameraComponent& a_component);

        /// @brief MeshFilterComponent の変更を CQRS 経由で要求する
        void submit_mesh_filter_component(
            GameCore::EntityId a_entityId,
            const ECS::MeshFilterComponent& a_component);

        /// @brief StaticMeshRendererComponent の変更を CQRS 経由で要求する
        void submit_static_mesh_renderer_component(
            GameCore::EntityId a_entityId,
            const ECS::StaticMeshRendererComponent& a_component);

        /// @brief Component 追加を CQRS 経由で要求する
        void submit_add_component(GameCore::EntityId a_entityId, ComponentKind a_kind);

        GameCore::GameWorld* m_gameWorld = nullptr;
        DrawSystem::MeshPool* m_meshPool = nullptr;
        GameCore::EntityId* m_selectedEntityId = nullptr;
        Core::CQRS::Bridge* m_commandBridge = nullptr;
        Math::float3 m_rotationEulerDegrees = Math::float3::zero();
        Math::Quaternion m_rotationSource = Math::Quaternion::identity();
        GameCore::EntityId m_rotationEntityId = GameCore::k_invalidEntityId;
        bool m_hasRotationCache = false;
        bool m_isRotationEditing = false;
    };
} // namespace Cue::Editor
