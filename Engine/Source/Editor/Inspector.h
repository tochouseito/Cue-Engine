#pragma once

/// **********************************************************************
/// 選択中 GameObject の Component 情報を表示、編集する Editor View
/// **********************************************************************

// === Runtime includes ===
#include <GameCore/GameCoreTypes.h>

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
    struct StaticMeshRendererComponent;
    struct TransformComponent;
}

namespace Cue::Editor
{
    /// @brief EditorManager の選択状態を参照して GameObject の Component 情報を表示、編集する。
    class Inspector final
    {
    public:
        Inspector(Core::CQRS::Bridge* a_commandBridge,
                  GameCore::GameWorld* a_gameWorld,
                  GameCore::EntityId* a_selectedEntityId) noexcept;
        ~Inspector() = default;

        /// @brief 表示対象の GameWorld を差し替える。
        void set_game_world(GameCore::GameWorld* a_gameWorld) noexcept;

        /// @brief Inspector window を描画する。
        void update();

    private:
        /// @brief 選択中 Entity の GameObject handle を探す。
        [[nodiscard]] bool find_selected_object(GameCore::GameObject& a_outObject);

        void draw_base_component(GameCore::GameObject& a_object);
        void draw_transform_component(GameCore::GameObject& a_object);
        void draw_world_transform_component(GameCore::GameObject& a_object);
        void draw_camera_component(GameCore::GameObject& a_object);
        void draw_mesh_filter_component(GameCore::GameObject& a_object);
        void draw_static_mesh_renderer_component(GameCore::GameObject& a_object);
        void draw_renderable_info_component(GameCore::GameObject& a_object);
        void submit_transform_component(
            GameCore::EntityId a_entityId,
            const ECS::TransformComponent& a_component);
        void submit_camera_component(
            GameCore::EntityId a_entityId,
            const ECS::CameraComponent& a_component);
        void submit_static_mesh_renderer_component(
            GameCore::EntityId a_entityId,
            const ECS::StaticMeshRendererComponent& a_component);

        GameCore::GameWorld* m_gameWorld = nullptr;
        GameCore::EntityId* m_selectedEntityId = nullptr;
        Core::CQRS::Bridge* m_commandBridge = nullptr;
    };
} // namespace Cue::Editor
