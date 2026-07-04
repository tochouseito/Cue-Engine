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
    /// @brief GameWorld を読み取り、選択可能な GameObject ツリーとして表示する。
    class Hierarchy final
    {
    public:
        Hierarchy(Core::CQRS::Bridge* a_commandBridge,
                  GameCore::GameWorld* a_gameWorld,
                  GameCore::EntityId* a_selectedEntityId,
                  GameCore::SceneId* a_selectedSceneId) noexcept;
        ~Hierarchy() = default;

        /// @brief 表示対象の GameWorld を差し替える。
        void set_game_world(GameCore::GameWorld* a_gameWorld) noexcept;

        /// @brief Hierarchy window を描画する。
        void update();

    private:
        struct ObjectEntry final
        {
            std::string name{};
            GameCore::EntityId entityId = GameCore::k_invalidEntityId;
            GameCore::EntityId parent = GameCore::k_invalidEntityId;
            GameCore::SceneId sceneId = GameCore::k_invalidSceneId;
            std::vector<size_t> children{};
        };

        struct DragObjectPayload final
        {
            GameCore::EntityId entityId = GameCore::k_invalidEntityId;
            GameCore::SceneId sceneId = GameCore::k_invalidSceneId;
        };

        /// @brief GameWorld から表示用の一時ツリーを再構築する。
        [[nodiscard]] bool refresh_objects();

        /// @brief 選択中 Object が存在しない場合は選択を解除する。
        void validate_selection() noexcept;

        /// @brief 指定 Object と子階層を描画する。
        void draw_object_node(size_t a_objectIndex);
        void draw_rename_input(const ObjectEntry& a_object);
        void draw_world_drop_target();
        void draw_object_drag_source(const ObjectEntry& a_object);
        void draw_object_drop_target(const ObjectEntry& a_object);
        void draw_object_context_menu(const ObjectEntry& a_object);
        void begin_rename(const ObjectEntry& a_object);
        void cancel_rename() noexcept;
        void submit_rename_command(GameCore::EntityId a_entityId);
        void submit_delete_command(GameCore::EntityId a_entityId);
        void submit_parent_command(GameCore::EntityId a_entityId, GameCore::EntityId a_parentId);
        [[nodiscard]] bool is_descendant_of(
            GameCore::EntityId a_entityId,
            GameCore::EntityId a_possibleAncestor) const noexcept;

        [[nodiscard]] GameCore::EntityId selected_entity_id() const noexcept;
        void set_selected_entity_id(GameCore::EntityId a_entityId) noexcept;
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
