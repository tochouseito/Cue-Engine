#pragma once

/// **********************************************************************
/// GameWorld の GameObject 階層を表示する Editor View
/// **********************************************************************

// === Runtime includes ===
#include <GameCore/GameCoreTypes.h>

// === C++ includes ===
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace Cue::GameCore
{
    class GameWorld;
}

namespace Cue::Editor
{
    /// @brief GameWorld を読み取り、選択可能な GameObject ツリーとして表示する。
    class Hierarchy final
    {
    public:
        Hierarchy(GameCore::GameWorld* a_gameWorld,
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

        /// @brief GameWorld から表示用の一時ツリーを再構築する。
        [[nodiscard]] bool refresh_objects();

        /// @brief 選択中 Object が存在しない場合は選択を解除する。
        void validate_selection() noexcept;

        /// @brief 指定 Object と子階層を描画する。
        void draw_object_node(size_t a_objectIndex);

        [[nodiscard]] GameCore::EntityId selected_entity_id() const noexcept;
        void set_selected_entity_id(GameCore::EntityId a_entityId) noexcept;
        void set_selected_scene_id(GameCore::SceneId a_sceneId) noexcept;

        GameCore::GameWorld* m_gameWorld = nullptr;
        GameCore::EntityId* m_selectedEntityId = nullptr;
        GameCore::SceneId* m_selectedSceneId = nullptr;

        std::vector<ObjectEntry> m_objects{};
        std::vector<size_t> m_roots{};
        std::unordered_map<GameCore::EntityId, size_t> m_objectIndexById{};
    };
} // namespace Cue::Editor
