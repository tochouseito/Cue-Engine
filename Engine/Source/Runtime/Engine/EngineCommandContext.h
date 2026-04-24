#pragma once

// === Engine includes ===
#include "Commands.h"
#include "GameCore/GameWorld.h"

// === C++ includes ===
#include <string_view>

namespace Cue
{
    class EngineCommandContext final : public IGameCommandContext
    {
    public:
        explicit EngineCommandContext(GameCore::GameWorld& a_gameWorld,
            GameCore::SceneId a_currentSceneId = GameCore::k_invalidSceneId) noexcept;

        Result create_object(GameCore::EntityId& a_outObjectId) override;
        Result destroy_object(GameCore::EntityId a_objectId) override;
        Result resolve_render_object_entity(
            uint32_t a_objectId, GameCore::EntityId& a_outEntityId) override;
        Result set_main_camera(uint32_t a_cameraIndex) override;
        Result get_object_name(
            GameCore::EntityId a_objectId, std::string& a_outName) override;
        Result rename_object(
            GameCore::EntityId a_objectId, std::string_view a_name) override;
        Result capture_deleted_object(
            GameCore::EntityId a_objectId,
            GameCore::DeletedObjectSnapshot& a_outSnapshot) override;
        Result restore_deleted_object(
            const GameCore::DeletedObjectSnapshot& a_snapshot,
            GameCore::EntityId& a_outObjectId) override;
        Result add_component(GameCore::EntityId a_objectId,
            AddableComponentType a_componentType) override;
        Result remove_component(GameCore::EntityId a_objectId,
            AddableComponentType a_componentType) override;
        Result get_transform_component(GameCore::EntityId a_objectId,
            ECS::TransformComponent& a_outComponent) override;
        Result set_transform_component(GameCore::EntityId a_objectId,
            const ECS::TransformComponent& a_component) override;
        Result get_script_component(GameCore::EntityId a_objectId,
            ECS::ScriptComponent& a_outComponent) override;
        Result set_script_component(GameCore::EntityId a_objectId,
            const ECS::ScriptComponent& a_component) override;

    private:
        template <typename T>
        Result add_component_internal(GameCore::EntityId a_objectId,
            const char* a_alreadyExistsMessage)
        {
            bool hasComponent = false;
            Result hasResult = m_gameWorld.has_component<T>(a_objectId, hasComponent);
            if (!hasResult)
            {
                return hasResult;
            }

            if (hasComponent)
            {
                return Result::fail(Code::InvalidState, Severity::Warning,
                    a_alreadyExistsMessage);
            }

            T* component = nullptr;
            return m_gameWorld.add_component<T>(a_objectId, component);
        }

        template <typename T>
        Result remove_component_internal(GameCore::EntityId a_objectId,
            const char* a_notFoundMessage)
        {
            bool hasComponent = false;
            Result hasResult = m_gameWorld.has_component<T>(a_objectId, hasComponent);
            if (!hasResult)
            {
                return hasResult;
            }

            if (!hasComponent)
            {
                return Result::fail(Code::NotFound, Severity::Warning,
                    a_notFoundMessage);
            }

            return m_gameWorld.remove_component<T>(a_objectId);
        }

        GameCore::GameWorld& m_gameWorld;
        GameCore::SceneId m_currentSceneId = GameCore::k_invalidSceneId;
    };
}
