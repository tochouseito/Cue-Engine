#include "EngineCommandContext.h"

namespace Cue
{
    EngineCommandContext::EngineCommandContext(
        GameCore::GameWorld& a_gameWorld,
        GameCore::SceneId a_currentSceneId) noexcept
        : m_gameWorld(a_gameWorld)
        , m_currentSceneId(a_currentSceneId)
    {
    }

    Result EngineCommandContext::create_object(AddObjectType a_objectType,
        GameCore::EntityId& a_outObjectId)
    {
        return create_object(a_objectType, m_currentSceneId, a_outObjectId);
    }

    Result EngineCommandContext::create_object(
        AddObjectType a_objectType,
        GameCore::SceneId a_sceneId,
        GameCore::EntityId& a_outObjectId)
    {
        GameCore::GameObject object{};
        Result result = Result::ok();
        switch (a_objectType)
        {
        case AddObjectType::Camera:
            result = a_sceneId != GameCore::k_invalidSceneId
                ? m_gameWorld.add_camera_object_to_scene(a_sceneId, object)
                : m_gameWorld.add_camera_object(object);
            break;
        case AddObjectType::StaticMesh3D:
            result = a_sceneId != GameCore::k_invalidSceneId
                ? m_gameWorld.add_object_to_scene(a_sceneId, object)
                : m_gameWorld.add_object(object);
            break;
        case AddObjectType::Sprite2D:
            result = a_sceneId != GameCore::k_invalidSceneId
                ? m_gameWorld.add_sprite_object_to_scene(a_sceneId, object)
                : m_gameWorld.add_sprite_object(object);
            break;
        case AddObjectType::DirectionalLight:
            result = a_sceneId != GameCore::k_invalidSceneId
                ? m_gameWorld.add_directional_light_object_to_scene(
                    a_sceneId, object)
                : m_gameWorld.add_directional_light_object(object);
            break;
        case AddObjectType::PointLight:
            result = a_sceneId != GameCore::k_invalidSceneId
                ? m_gameWorld.add_point_light_object_to_scene(a_sceneId, object)
                : m_gameWorld.add_point_light_object(object);
            break;
        case AddObjectType::SpotLight:
            result = a_sceneId != GameCore::k_invalidSceneId
                ? m_gameWorld.add_spot_light_object_to_scene(a_sceneId, object)
                : m_gameWorld.add_spot_light_object(object);
            break;
        }

        a_outObjectId = result ? object.entity_id() : GameCore::k_invalidEntityId;
        return result;
    }

    Result EngineCommandContext::destroy_object(GameCore::EntityId a_objectId)
    {
        return m_gameWorld.destroy_object(a_objectId);
    }

    Result EngineCommandContext::resolve_render_object_entity(
        uint32_t a_objectId,
        GameCore::EntityId& a_outEntityId)
    {
        return m_gameWorld.get_render_object_entity(a_objectId, a_outEntityId);
    }

    Result EngineCommandContext::set_main_camera(
        GameCore::EntityId a_cameraEntityId)
    {
        return m_gameWorld.set_main_camera(a_cameraEntityId);
    }

    Result EngineCommandContext::get_object_name(
        GameCore::EntityId a_objectId,
        std::string& a_outName)
    {
        return m_gameWorld.get_object_name(a_objectId, a_outName);
    }

    Result EngineCommandContext::rename_object(
        GameCore::EntityId a_objectId,
        std::string_view a_name)
    {
        return m_gameWorld.set_object_name(a_objectId, a_name);
    }

    Result EngineCommandContext::capture_deleted_object(
        GameCore::EntityId a_objectId,
        GameCore::DeletedObjectSnapshot& a_outSnapshot)
    {
        return m_gameWorld.capture_deleted_object(a_objectId, a_outSnapshot);
    }

    Result EngineCommandContext::restore_deleted_object(
        const GameCore::DeletedObjectSnapshot& a_snapshot,
        GameCore::EntityId& a_outObjectId)
    {
        return m_gameWorld.restore_deleted_object(a_snapshot, a_outObjectId);
    }

    Result EngineCommandContext::add_component(
        GameCore::EntityId a_objectId,
        AddableComponentType a_componentType)
    {
        switch (a_componentType)
        {
        case AddableComponentType::Camera:
            return add_component_internal<ECS::CameraComponent>(
                a_objectId, "CameraComponent already exists.");

        case AddableComponentType::MeshFilter:
            return add_component_internal<ECS::MeshFilterComponent>(
                a_objectId, "MeshFilterComponent already exists.");

        case AddableComponentType::StaticMeshRenderer:
            return add_component_internal<ECS::StaticMeshRendererComponent>(
                a_objectId, "StaticMeshRendererComponent already exists.");

        case AddableComponentType::SpriteRenderer:
            return add_component_internal<ECS::SpriteRendererComponent>(
                a_objectId, "SpriteRendererComponent already exists.");

        case AddableComponentType::AudioSource:
            return add_component_internal<ECS::AudioSourceComponent>(
                a_objectId, "AudioSourceComponent already exists.");

        case AddableComponentType::RigidBody:
            return add_component_internal<ECS::RigidBodyComponent>(
                a_objectId, "RigidBodyComponent already exists.");

        case AddableComponentType::Collider:
            return add_component_internal<ECS::ColliderComponent>(
                a_objectId, "ColliderComponent already exists.");

        case AddableComponentType::CharacterController:
            return add_component_internal<ECS::CharacterControllerComponent>(
                a_objectId, "CharacterControllerComponent already exists.");

        case AddableComponentType::DirectionalLight:
            return add_component_internal<ECS::DirectionalLightComponent>(
                a_objectId, "DirectionalLightComponent already exists.");

        case AddableComponentType::PointLight:
            return add_component_internal<ECS::PointLightComponent>(
                a_objectId, "PointLightComponent already exists.");

        case AddableComponentType::SpotLight:
            return add_component_internal<ECS::SpotLightComponent>(
                a_objectId, "SpotLightComponent already exists.");

        case AddableComponentType::Script:
            return add_component_internal<ECS::ScriptComponent>(
                a_objectId, "ScriptComponent already exists.");
        }

        return Result::fail(Code::InvalidArgument, Severity::Error,
            "Unknown component type was requested.");
    }

    Result EngineCommandContext::remove_component(
        GameCore::EntityId a_objectId,
        AddableComponentType a_componentType)
    {
        switch (a_componentType)
        {
        case AddableComponentType::Camera:
            return remove_component_internal<ECS::CameraComponent>(
                a_objectId, "CameraComponent was not found.");

        case AddableComponentType::MeshFilter:
            return remove_component_internal<ECS::MeshFilterComponent>(
                a_objectId, "MeshFilterComponent was not found.");

        case AddableComponentType::StaticMeshRenderer:
            return remove_component_internal<ECS::StaticMeshRendererComponent>(
                a_objectId, "StaticMeshRendererComponent was not found.");

        case AddableComponentType::SpriteRenderer:
            return remove_component_internal<ECS::SpriteRendererComponent>(
                a_objectId, "SpriteRendererComponent was not found.");

        case AddableComponentType::AudioSource:
            return remove_component_internal<ECS::AudioSourceComponent>(
                a_objectId, "AudioSourceComponent was not found.");

        case AddableComponentType::RigidBody:
            return remove_component_internal<ECS::RigidBodyComponent>(
                a_objectId, "RigidBodyComponent was not found.");

        case AddableComponentType::Collider:
            return remove_component_internal<ECS::ColliderComponent>(
                a_objectId, "ColliderComponent was not found.");

        case AddableComponentType::CharacterController:
            return remove_component_internal<ECS::CharacterControllerComponent>(
                a_objectId, "CharacterControllerComponent was not found.");

        case AddableComponentType::DirectionalLight:
            return remove_component_internal<ECS::DirectionalLightComponent>(
                a_objectId, "DirectionalLightComponent was not found.");

        case AddableComponentType::PointLight:
            return remove_component_internal<ECS::PointLightComponent>(
                a_objectId, "PointLightComponent was not found.");

        case AddableComponentType::SpotLight:
            return remove_component_internal<ECS::SpotLightComponent>(
                a_objectId, "SpotLightComponent was not found.");

        case AddableComponentType::Script:
            return remove_component_internal<ECS::ScriptComponent>(
                a_objectId, "ScriptComponent was not found.");
        }

        return Result::fail(Code::InvalidArgument, Severity::Error,
            "Unknown component type was requested.");
    }

    Result EngineCommandContext::get_transform_component(
        GameCore::EntityId a_objectId,
        ECS::TransformComponent& a_outComponent)
    {
        ECS::TransformComponent* component = nullptr;
        Result result = m_gameWorld.get_component<ECS::TransformComponent>(
            a_objectId, component);
        if (!result || component == nullptr)
        {
            return result;
        }

        a_outComponent = *component;
        return Result::ok();
    }

    Result EngineCommandContext::set_transform_component(
        GameCore::EntityId a_objectId,
        const ECS::TransformComponent& a_component)
    {
        ECS::TransformComponent* component = nullptr;
        Result result = m_gameWorld.get_component<ECS::TransformComponent>(
            a_objectId, component);
        if (!result || component == nullptr)
        {
            return result;
        }

        *component = a_component;
        return Result::ok();
    }

    Result EngineCommandContext::get_script_component(
        GameCore::EntityId a_objectId,
        ECS::ScriptComponent& a_outComponent)
    {
        ECS::ScriptComponent* component = nullptr;
        Result result =
            m_gameWorld.get_component<ECS::ScriptComponent>(a_objectId, component);
        if (!result || component == nullptr)
        {
            return result;
        }

        a_outComponent = *component;
        return Result::ok();
    }

    Result EngineCommandContext::set_script_component(
        GameCore::EntityId a_objectId,
        const ECS::ScriptComponent& a_component)
    {
        ECS::ScriptComponent* component = nullptr;
        Result result =
            m_gameWorld.get_component<ECS::ScriptComponent>(a_objectId, component);
        if (!result || component == nullptr)
        {
            return result;
        }

        *component = a_component;
        return Result::ok();
    }
}
