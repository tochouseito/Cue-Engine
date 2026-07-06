#pragma once

/// **********************************************************************
/// GameWorld の GameObject 階層を表示する Editor View
/// **********************************************************************

// === Runtime includes ===
#include <GameCore/GameCoreTypes.h>

// === C++ includes ===
#include <array>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace Cue::GameCore
{
    class GameWorld;
}

namespace Cue::Core::CQRS
{
    class Bridge;
}

namespace Cue::Editor
{
    /// @brief GameWorld を読み取り、選択可能な GameObject ツリーとして表示する
    class Hierarchy final
    {
    public:
        /// @brief Editor 共有状態を参照して Hierarchy View を構築する
        /// @param a_commandBridge GameWorld 編集コマンドの送信先
        /// @param a_gameWorld 表示対象の GameWorld
        /// @param a_selectedEntityId Editor 全体で共有する選択 Entity
        /// @param a_selectedSceneId Editor 全体で共有する選択 Scene
        Hierarchy(Core::CQRS::Bridge* a_commandBridge,
                  GameCore::GameWorld* a_gameWorld,
                  GameCore::EntityId* a_selectedEntityId,
                  GameCore::SceneId* a_selectedSceneId) noexcept;
        ~Hierarchy() = default;

        /// @brief 表示対象の GameWorld を差し替える
        void set_game_world(GameCore::GameWorld* a_gameWorld) noexcept;

        /// @brief Hierarchy window を描画する
        void update();

    private:
        /// @brief ImGui 描画用に GameObject の親子関係を平坦化した一時要素
        struct ObjectEntry final
        {
            std::string name{};
            GameCore::EntityId entityId = GameCore::k_invalidEntityId;
            GameCore::EntityId parent = GameCore::k_invalidEntityId;
            GameCore::SceneId sceneId = GameCore::k_invalidSceneId;
            std::vector<size_t> children{};
        };

        /// @brief ImGui payload はポインタ寿命を持たないため Entity と Scene だけを渡す
        struct DragObjectPayload final
        {
            GameCore::EntityId entityId = GameCore::k_invalidEntityId;
            GameCore::SceneId sceneId = GameCore::k_invalidSceneId;
        };

        /// @brief GameWorld から表示用の一時ツリーを再構築する
        [[nodiscard]] bool refresh_objects();

        /// @brief 選択中 Object が存在しない場合は選択を解除する
        void validate_selection() noexcept;

        /// @brief 指定 Object と子階層を描画する
        void draw_object_node(size_t a_objectIndex);

        /// @brief rename 中の Object 名をインライン入力として描画する
        void draw_rename_input(const ObjectEntry& a_object);

        /// @brief root への drop を受け取り親子関係を解除する
        void draw_world_drop_target();

        /// @brief Object を親子付け替え payload として drag する
        void draw_object_drag_source(const ObjectEntry& a_object);

        /// @brief 同一 Scene 内の循環しない親子付け替えだけを受け付ける
        void draw_object_drop_target(const ObjectEntry& a_object);

        /// @brief Object 単位の編集コマンドを context menu から送信する
        void draw_object_context_menu(const ObjectEntry& a_object);

        /// @brief 選択状態と入力 buffer を rename 開始状態へ同期する
        void begin_rename(const ObjectEntry& a_object);

        /// @brief rename 入力の一時状態を破棄する
        void cancel_rename() noexcept;

        /// @brief CQRS 経由で rename を要求する
        void submit_rename_command(GameCore::EntityId a_entityId);

        /// @brief CQRS 経由で Object 削除を要求する
        void submit_delete_command(GameCore::EntityId a_entityId);

        /// @brief CQRS 経由で親子関係の変更を要求する
        void submit_parent_command(GameCore::EntityId a_entityId, GameCore::EntityId a_parentId);

        /// @brief drag and drop による循環参照を防ぐため祖先関係を確認する
        [[nodiscard]] bool is_descendant_of(
            GameCore::EntityId a_entityId,
            GameCore::EntityId a_possibleAncestor) const noexcept;

        /// @brief 選択状態が未接続の場合は invalid を返す
        [[nodiscard]] GameCore::EntityId selected_entity_id() const noexcept;

        /// @brief Editor 共有の選択 Entity を更新する
        void set_selected_entity_id(GameCore::EntityId a_entityId) noexcept;

        /// @brief Editor 共有の選択 Scene を更新する
        void set_selected_scene_id(GameCore::SceneId a_sceneId) noexcept;

        static constexpr const char* k_objectDragPayloadType = "CueHierarchyObject";
        Core::CQRS::Bridge* m_commandBridge = nullptr;
        GameCore::GameWorld* m_gameWorld = nullptr;
        GameCore::EntityId* m_selectedEntityId = nullptr;
        GameCore::SceneId* m_selectedSceneId = nullptr;

        std::vector<ObjectEntry> m_objects{};
        std::vector<size_t> m_roots{};
        std::unordered_map<GameCore::EntityId, size_t> m_objectIndexById{};
        GameCore::EntityId m_renamingEntityId = GameCore::k_invalidEntityId;
        std::array<char, 256> m_renameBuffer{};
        bool m_focusRenameInput = false;
    };
} // namespace Cue::Editor
