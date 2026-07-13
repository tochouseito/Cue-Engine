#pragma once

/// **********************************************************************
/// 選択中 GameObject の Component 情報を表示、編集する Editor View
/// **********************************************************************

// === Runtime includes ===
#include <GameCore/GameCoreTypes.h>

// === Editor includes ===
#include "AssetSelection.h"

// === Math includes ===
#include <CueMath.h>

// === C++ includes ===
#include <array>
#include <cstdint>
#include <string>

namespace Cue
{
    enum class ComponentKind : uint8_t;
}

namespace Cue::GameCore
{
    class GameObject;
    class GameWorld;
} // namespace Cue::GameCore

namespace Cue::Core::CQRS
{
    class Bridge;
}

namespace Cue::ECS
{
    struct CameraComponent;
    struct MeshFilterComponent;
    struct ScriptComponent;
    struct StaticMeshRendererComponent;
    struct TransformComponent;
} // namespace Cue::ECS

namespace Cue::DrawSystem
{
    class MeshPool;
}

namespace Cue::Editor
{
    /// @brief EditorManager の選択状態を参照して GameObject の Component
    /// 情報を表示、編集する
    class Inspector final
    {
      public:
        /// @brief Editor 共有状態を参照して Inspector View を構築する
        /// @param a_commandBridge Component 編集コマンドの送信先
        /// @param a_gameWorld 表示対象の GameWorld
        /// @param a_meshPool MeshFilter に割り当てる mesh の一覧取得元
        /// @param a_selectedEntityId Editor 全体で共有する選択 Entity
        /// @param a_selectedAsset Entity 選択と分離された Asset 選択
        Inspector(Core::CQRS::Bridge* a_commandBridge,
                  GameCore::GameWorld* a_gameWorld, DrawSystem::MeshPool* a_meshPool,
                  GameCore::EntityId* a_selectedEntityId,
                  AssetSelection* a_selectedAsset) noexcept;
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

        /// @brief 選択された Asset の識別情報を読み取り専用で表示する
        void draw_asset_selection();

        /// @brief 選択中 GameObject に追加できる Component の menu を描画する
        void draw_add_component_menu(GameCore::GameObject& a_object);

        /// @brief Scene 所有や親子関係を表示し、編集可能な Object 基礎情報を CQRS へ送信する
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

        /// @brief Script class と有効状態を編集し、Play 時の instance 同期対象を設定する
        void draw_script_component(GameCore::GameObject& a_object);

        /// @brief GPU 描画用に割り当てられた ID を読み取り専用で表示する
        void draw_renderable_info_component(GameCore::GameObject& a_object);

        /// @brief TransformComponent の変更を CQRS 経由で要求する
        void submit_transform_component(GameCore::EntityId a_entityId,
                                        const ECS::TransformComponent& a_component,
                                        uint64_t a_historyTransactionId = 0);

        /// @brief Quaternion から Editor 表示用 Euler 角を同期する
        void sync_rotation_cache(GameCore::EntityId a_entityId,
                                 const ECS::TransformComponent& a_component) noexcept;

        /// @brief CameraComponent の変更を CQRS 経由で要求する
        void submit_camera_component(GameCore::EntityId a_entityId,
                                     const ECS::CameraComponent& a_component,
                                     uint64_t a_historyTransactionId = 0);

        /// @brief MeshFilterComponent の変更を CQRS 経由で要求する
        void
        submit_mesh_filter_component(GameCore::EntityId a_entityId,
                                     const ECS::MeshFilterComponent& a_component);

        /// @brief StaticMeshRendererComponent の変更を CQRS 経由で要求する
        void submit_static_mesh_renderer_component(
            GameCore::EntityId a_entityId,
            const ECS::StaticMeshRendererComponent& a_component,
            uint64_t a_historyTransactionId = 0);

        /// @brief ScriptComponent の変更を CQRS 経由で要求する
        void submit_script_component(
            GameCore::EntityId a_entityId,
            const ECS::ScriptComponent& a_component,
            uint64_t a_historyTransactionId = 0);

        /// @brief 現在の ImGui 入力操作に対応する履歴統合用 ID を取得する
        [[nodiscard]] uint64_t current_history_transaction();

        /// @brief Component header の削除操作を CQRS 経由で要求する
        void draw_remove_component_button(GameCore::EntityId a_entityId,
                                          ComponentKind a_kind,
                                          const char* a_componentName);

        /// @brief Component 追加を CQRS 経由で要求する
        void submit_add_component(GameCore::EntityId a_entityId,
                                  ComponentKind a_kind);

        /// @brief Component 削除を CQRS 経由で要求する
        void submit_remove_component(GameCore::EntityId a_entityId,
                                     ComponentKind a_kind);

        /// @brief Object 名の変更を CQRS 経由で要求する
        void submit_rename_object(GameCore::EntityId a_entityId, std::string a_name);

        /// @brief Object Tag の変更を CQRS 経由で要求する
        void submit_object_tag(GameCore::EntityId a_entityId, std::string a_tag);

        /// @brief Object ActiveSelf の変更を CQRS 経由で要求する
        void submit_object_active(GameCore::EntityId a_entityId, bool a_isActive);

        /// @brief Object Persistent の変更を CQRS 経由で要求する
        void submit_object_persistent(GameCore::EntityId a_entityId, bool a_isPersistent);

        GameCore::GameWorld* m_gameWorld = nullptr;
        DrawSystem::MeshPool* m_meshPool = nullptr;
        GameCore::EntityId* m_selectedEntityId = nullptr;
        AssetSelection* m_selectedAsset = nullptr;
        Core::CQRS::Bridge* m_commandBridge = nullptr;
        Math::float3 m_rotationEulerDegrees = Math::float3::zero();
        Math::Quaternion m_rotationSource = Math::Quaternion::identity();
        GameCore::EntityId m_rotationEntityId = GameCore::k_invalidEntityId;
        GameCore::EntityId m_baseEntityId = GameCore::k_invalidEntityId;
        GameCore::EntityId m_scriptEntityId = GameCore::k_invalidEntityId;
        std::array<char, 256> m_nameBuffer{};
        std::array<char, 256> m_scriptClassNameBuffer{};
        std::array<char, 256> m_tagBuffer{};
        uint64_t m_nextHistoryTransactionId = 1;
        uint64_t m_activeHistoryTransactionId = 0;
        uint32_t m_activeHistoryItemId = 0;
        bool m_hasRotationCache = false;
        bool m_isRotationEditing = false;
        bool m_isNameEditing = false;
        bool m_isTagEditing = false;
    };
} // namespace Cue::Editor
