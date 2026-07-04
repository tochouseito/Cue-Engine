#pragma once

/// **********************************************************************
/// Engine API Command
/// **********************************************************************

// === Engine include ===
#include "Commands.h"
#include <GameCore/GameWorld.h>

// === C++ includes ===
#include <string_view>

namespace Cue
{
    class EngineCommandContext final : public IGameCommandContext
    {
    public:
        explicit EngineCommandContext(GameCore::GameWorld& a_gameWorld) noexcept;

        Result destroy_object(GameCore::EntityId a_objectId) override;
        Result get_object_name(GameCore::EntityId a_objectId, std::string& a_outName) override;
        Result rename_object(GameCore::EntityId a_objectId, std::string_view a_name) override;
        Result get_parent(GameCore::EntityId a_objectId, GameCore::EntityId& a_outParentId) override;
        Result set_parent(
            GameCore::EntityId a_objectId,
            GameCore::EntityId a_parentId,
            bool a_keepsWorldTransform) override;
        Result get_transform_component(
            GameCore::EntityId a_objectId,
            ECS::TransformComponent& a_outComponent) override;
        Result set_transform_component(
            GameCore::EntityId a_objectId,
            const ECS::TransformComponent& a_component) override;
        Result get_camera_component(
            GameCore::EntityId a_objectId,
            ECS::CameraComponent& a_outComponent) override;
        Result set_camera_component(
            GameCore::EntityId a_objectId,
            const ECS::CameraComponent& a_component) override;
        Result get_static_mesh_renderer_component(
            GameCore::EntityId a_objectId,
            ECS::StaticMeshRendererComponent& a_outComponent) override;
        Result set_static_mesh_renderer_component(
            GameCore::EntityId a_objectId,
            const ECS::StaticMeshRendererComponent& a_component) override;

    private:
        GameCore::GameWorld& m_gameWorld;
    };
}
