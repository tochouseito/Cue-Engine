#include "EngineCommandContext.h"

// === Engine includes ===
#include "GameCore/ObjectSnapshotService.h"
#include "GameCore/TransformSystem.h"

// === C++ includes ===
#include <memory>

namespace Cue
{
    namespace
    {
        template <typename Value, typename Getter, typename Setter>
        class SetObjectValueCommand final : public Core::CQRS::IUndoableCommand
        {
        public:
            SetObjectValueCommand(
                GameCore::EntityId a_objectId,
                Value a_newValue,
                Getter a_getter,
                Setter a_setter,
                const char* a_notExecutedMessage)
                : m_oldValue()
                , m_newValue(std::move(a_newValue))
                , m_objectId(a_objectId)
                , m_getter(a_getter)
                , m_setter(a_setter)
                , m_notExecutedMessage(a_notExecutedMessage)
            {
            }

            Result execute(Core::CQRS::ICommandContext& a_commandContext) override
            {
                IGameCommandContext* gameCommandContext =
                    dynamic_cast<IGameCommandContext*>(&a_commandContext);
                if (gameCommandContext == nullptr)
                {
                    return Result::fail(
                        Code::InvalidArgument,
                        Severity::Error,
                        "Command context does not support object property updates.");
                }

                if (!m_hasOldValue)
                {
                    Result captureResult =
                        (gameCommandContext->*m_getter)(m_objectId, m_oldValue);
                    if (!captureResult)
                    {
                        return captureResult;
                    }

                    m_hasOldValue = true;
                }

                return (gameCommandContext->*m_setter)(m_objectId, m_newValue);
            }

            Result undo(Core::CQRS::ICommandContext& a_commandContext) override
            {
                IGameCommandContext* gameCommandContext =
                    dynamic_cast<IGameCommandContext*>(&a_commandContext);
                if (gameCommandContext == nullptr)
                {
                    return Result::fail(
                        Code::InvalidArgument,
                        Severity::Error,
                        "Command context does not support object property update undo.");
                }

                if (!m_hasOldValue)
                {
                    return Result::fail(Code::InvalidState, Severity::Error, m_notExecutedMessage);
                }

                return (gameCommandContext->*m_setter)(m_objectId, m_oldValue);
            }

        private:
            Value m_oldValue{};
            Value m_newValue{};
            GameCore::EntityId m_objectId = GameCore::k_invalidEntityId;
            Getter m_getter = nullptr;
            Setter m_setter = nullptr;
            const char* m_notExecutedMessage = nullptr;
            bool m_hasOldValue = false;
        };

        template <typename Value, typename Getter, typename Setter>
        [[nodiscard]] std::unique_ptr<Core::CQRS::ICommand> make_set_object_value_command(
            GameCore::EntityId a_objectId,
            Value a_newValue,
            Getter a_getter,
            Setter a_setter,
            const char* a_notExecutedMessage)
        {
            return std::make_unique<SetObjectValueCommand<Value, Getter, Setter>>(
                a_objectId, std::move(a_newValue), a_getter, a_setter, a_notExecutedMessage);
        }

        template <typename Component>
        class SetComponentCommand final : public Core::CQRS::IUndoableCommand
        {
        public:
            using Getter = Result (IGameCommandContext::*)(
                GameCore::EntityId,
                Component&);
            using Setter = Result (IGameCommandContext::*)(
                GameCore::EntityId,
                const Component&);

            SetComponentCommand(
                GameCore::EntityId a_objectId,
                const Component& a_newComponent,
                Getter a_getter,
                Setter a_setter,
                const char* a_notExecutedMessage) noexcept
                : m_oldComponent()
                , m_newComponent(a_newComponent)
                , m_objectId(a_objectId)
                , m_getter(a_getter)
                , m_setter(a_setter)
                , m_notExecutedMessage(a_notExecutedMessage)
            {
            }

            Result execute(Core::CQRS::ICommandContext& a_commandContext) override
            {
                IGameCommandContext* gameCommandContext =
                    dynamic_cast<IGameCommandContext*>(&a_commandContext);
                if (gameCommandContext == nullptr)
                {
                    return Result::fail(
                        Code::InvalidArgument,
                        Severity::Error,
                        "Command context does not support component updates.");
                }

                if (!m_hasOldComponent)
                {
                    // undo は最初の実行時点の Component snapshot へ戻す。
                    Result captureResult =
                        (gameCommandContext->*m_getter)(m_objectId, m_oldComponent);
                    if (!captureResult)
                    {
                        return captureResult;
                    }

                    m_hasOldComponent = true;
                }

                return (gameCommandContext->*m_setter)(m_objectId, m_newComponent);
            }

            Result undo(Core::CQRS::ICommandContext& a_commandContext) override
            {
                IGameCommandContext* gameCommandContext =
                    dynamic_cast<IGameCommandContext*>(&a_commandContext);
                if (gameCommandContext == nullptr)
                {
                    return Result::fail(
                        Code::InvalidArgument,
                        Severity::Error,
                        "Command context does not support component update undo.");
                }

                if (!m_hasOldComponent)
                {
                    return Result::fail(
                        Code::InvalidState,
                        Severity::Error,
                        m_notExecutedMessage);
                }

                return (gameCommandContext->*m_setter)(m_objectId, m_oldComponent);
            }

            bool try_merge(const Core::CQRS::IUndoableCommand& a_command) override
            {
                const auto* command = dynamic_cast<const SetComponentCommand*>(&a_command);
                if (command == nullptr || command->m_objectId != m_objectId ||
                    command->m_setter != m_setter)
                {
                    return false;
                }

                // 最初の Command が保持する編集前 snapshot を残し、Redo 用の最終値だけ更新する。
                m_newComponent = command->m_newComponent;
                return true;
            }

        private:
            Component m_oldComponent{};
            Component m_newComponent{};
            GameCore::EntityId m_objectId = GameCore::k_invalidEntityId;
            Getter m_getter = nullptr;
            Setter m_setter = nullptr;
            const char* m_notExecutedMessage = nullptr;
            bool m_hasOldComponent = false;
        };

        template <typename Component>
        Result add_component_if_missing(
            GameCore::GameWorld& a_gameWorld,
            GameCore::EntityId a_objectId,
            const char* a_existingMessage)
        {
            bool hasComponent = false;
            Result result = a_gameWorld.has_component<Component>(a_objectId, hasComponent);
            if (!result)
            {
                return result;
            }
            if (hasComponent)
            {
                return Result::fail(Code::InvalidState, Severity::Warning, a_existingMessage);
            }

            Component* component = nullptr;
            return a_gameWorld.add_component<Component>(a_objectId, component);
        }
    } // namespace

    std::unique_ptr<Core::CQRS::ICommand> make_set_object_tag_command(
        GameCore::EntityId a_objectId,
        std::string a_tag)
    {
        return make_set_object_value_command(
            a_objectId,
            std::move(a_tag),
            &IGameCommandContext::get_object_tag,
            &IGameCommandContext::set_object_tag,
            "Object tag command has not been executed.");
    }

    std::unique_ptr<Core::CQRS::ICommand> make_set_object_active_command(
        GameCore::EntityId a_objectId,
        bool a_isActive)
    {
        return make_set_object_value_command(
            a_objectId,
            a_isActive,
            &IGameCommandContext::get_object_active,
            &IGameCommandContext::set_object_active,
            "Object active command has not been executed.");
    }

    std::unique_ptr<Core::CQRS::ICommand> make_set_object_persistent_command(
        GameCore::EntityId a_objectId,
        bool a_isPersistent)
    {
        return make_set_object_value_command(
            a_objectId,
            a_isPersistent,
            &IGameCommandContext::get_object_persistent,
            &IGameCommandContext::set_object_persistent,
            "Object persistent command has not been executed.");
    }

    std::unique_ptr<Core::CQRS::ICommand> make_set_transform_component_command(
        GameCore::EntityId a_objectId,
        const ECS::TransformComponent& a_component)
    {
        return std::make_unique<SetComponentCommand<ECS::TransformComponent>>(
            a_objectId,
            a_component,
            &IGameCommandContext::get_transform_component,
            &IGameCommandContext::set_transform_component,
            "Transform component command has not been executed.");
    }

    std::unique_ptr<Core::CQRS::ICommand> make_set_camera_component_command(
        GameCore::EntityId a_objectId,
        const ECS::CameraComponent& a_component)
    {
        return std::make_unique<SetComponentCommand<ECS::CameraComponent>>(
            a_objectId,
            a_component,
            &IGameCommandContext::get_camera_component,
            &IGameCommandContext::set_camera_component,
            "Camera component command has not been executed.");
    }

    std::unique_ptr<Core::CQRS::ICommand> make_set_mesh_filter_component_command(
        GameCore::EntityId a_objectId,
        const ECS::MeshFilterComponent& a_component)
    {
        return std::make_unique<SetComponentCommand<ECS::MeshFilterComponent>>(
            a_objectId,
            a_component,
            &IGameCommandContext::get_mesh_filter_component,
            &IGameCommandContext::set_mesh_filter_component,
            "Mesh filter component command has not been executed.");
    }

    std::unique_ptr<Core::CQRS::ICommand> make_set_static_mesh_renderer_component_command(
        GameCore::EntityId a_objectId,
        const ECS::StaticMeshRendererComponent& a_component)
    {
        return std::make_unique<SetComponentCommand<ECS::StaticMeshRendererComponent>>(
            a_objectId,
            a_component,
            &IGameCommandContext::get_static_mesh_renderer_component,
            &IGameCommandContext::set_static_mesh_renderer_component,
            "Static mesh renderer component command has not been executed.");
    }

    EngineCommandContext::EngineCommandContext(
        GameCore::GameWorld& a_gameWorld,
        DrawSystem::RenderCameraSelection& a_cameraSelection) noexcept
        : m_gameWorld(a_gameWorld)
        , m_cameraSelection(a_cameraSelection)
    {
    }

    Result EngineCommandContext::create_object(std::string_view a_name, GameCore::EntityId& a_outObjectId)
    {
        a_outObjectId = GameCore::k_invalidEntityId;

        GameCore::GameObject object{};
        Result result = m_gameWorld.create_object(a_name, object);
        if (!result)
        {
            return result;
        }

        a_outObjectId = object.entity_id();
        GameCore::TransformSystem::sync_world_transforms(m_gameWorld);
        return Result::ok();
    }

    Result EngineCommandContext::destroy_object(GameCore::EntityId a_objectId)
    {
        Result result = m_gameWorld.destroy_object(a_objectId);
        if (result)
        {
            m_gameWorld.execute_deferred_deletions();
            if (m_cameraSelection.camera_entity() == a_objectId)
            {
                m_cameraSelection.clear();
            }
        }

        return result;
    }

    Result EngineCommandContext::add_component(GameCore::EntityId a_objectId, ComponentKind a_kind)
    {
        Result result = Result::ok();
        switch (a_kind)
        {
        case ComponentKind::Transform:
            result = add_component_if_missing<ECS::TransformComponent>(
                m_gameWorld,
                a_objectId,
                "TransformComponent already exists.");
            if (result)
            {
                GameCore::TransformSystem::sync_world_transforms(m_gameWorld);
            }
            return result;
        case ComponentKind::Camera:
            return add_component_if_missing<ECS::CameraComponent>(
                m_gameWorld,
                a_objectId,
                "CameraComponent already exists.");
        case ComponentKind::MeshFilter:
            return add_component_if_missing<ECS::MeshFilterComponent>(
                m_gameWorld,
                a_objectId,
                "MeshFilterComponent already exists.");
        case ComponentKind::StaticMeshRenderer:
            return add_component_if_missing<ECS::StaticMeshRendererComponent>(
                m_gameWorld,
                a_objectId,
                "StaticMeshRendererComponent already exists.");
        default:
            return Result::fail(Code::InvalidArgument, Severity::Error, "Unknown component kind.");
        }
    }

    Result EngineCommandContext::remove_component(GameCore::EntityId a_objectId, ComponentKind a_kind)
    {
        switch (a_kind)
        {
        case ComponentKind::Transform:
            return Result::fail(
                Code::InvalidState,
                Severity::Warning,
                "TransformComponent is required and cannot be removed.");
        case ComponentKind::Camera:
        {
            const Result result = m_gameWorld.remove_component<ECS::CameraComponent>(a_objectId);
            if (result && m_cameraSelection.camera_entity() == a_objectId)
            {
                m_cameraSelection.clear();
            }
            return result;
        }
        case ComponentKind::MeshFilter:
            return m_gameWorld.remove_component<ECS::MeshFilterComponent>(a_objectId);
        case ComponentKind::StaticMeshRenderer:
            return m_gameWorld.remove_component<ECS::StaticMeshRendererComponent>(a_objectId);
        default:
            return Result::fail(Code::InvalidArgument, Severity::Error, "Unknown component kind.");
        }
    }

    Result EngineCommandContext::get_render_camera(GameCore::EntityId& a_outObjectId)
    {
        a_outObjectId = m_cameraSelection.camera_entity();
        return Result::ok();
    }

    Result EngineCommandContext::set_render_camera(GameCore::EntityId a_objectId)
    {
        bool hasTransform = false;
        Result result = m_gameWorld.has_component<ECS::TransformComponent>(a_objectId, hasTransform);
        if (!result)
        {
            return result;
        }
        bool hasCamera = false;
        result = m_gameWorld.has_component<ECS::CameraComponent>(a_objectId, hasCamera);
        if (!result)
        {
            return result;
        }
        if (!hasTransform || !hasCamera)
        {
            return Result::fail(Code::InvalidState, Severity::Warning,
                                "Render camera requires TransformComponent and CameraComponent.");
        }

        m_cameraSelection.set_camera_entity(a_objectId);
        return Result::ok();
    }

    Result EngineCommandContext::capture_object_snapshot(
        GameCore::EntityId a_objectId,
        GameCore::ObjectSnapshot& a_outSnapshot)
    {
        return GameCore::ObjectSnapshotService::capture(
            m_gameWorld, m_cameraSelection, a_objectId, a_outSnapshot);
    }

    Result EngineCommandContext::restore_object_snapshot(
        const GameCore::ObjectSnapshot& a_snapshot,
        GameCore::EntityId& a_outObjectId)
    {
        return GameCore::ObjectSnapshotService::restore(
            m_gameWorld, m_cameraSelection, a_snapshot, a_outObjectId);
    }

    Result EngineCommandContext::get_object_name(GameCore::EntityId a_objectId, std::string& a_outName)
    {
        return m_gameWorld.get_object_name(a_objectId, a_outName);
    }

    Result EngineCommandContext::rename_object(GameCore::EntityId a_objectId, std::string_view a_name)
    {
        return m_gameWorld.set_object_name(a_objectId, a_name);
    }

    Result EngineCommandContext::get_object_tag(
        GameCore::EntityId a_objectId,
        std::string& a_outTag)
    {
        return m_gameWorld.get_object_tag(a_objectId, a_outTag);
    }

    Result EngineCommandContext::set_object_tag(
        GameCore::EntityId a_objectId,
        std::string_view a_tag)
    {
        return m_gameWorld.set_object_tag(a_objectId, a_tag);
    }

    Result EngineCommandContext::get_object_active(
        GameCore::EntityId a_objectId,
        bool& a_outIsActive)
    {
        return m_gameWorld.is_object_active(a_objectId, a_outIsActive);
    }

    Result EngineCommandContext::set_object_active(
        GameCore::EntityId a_objectId,
        bool a_isActive)
    {
        return m_gameWorld.set_object_active(a_objectId, a_isActive);
    }

    Result EngineCommandContext::get_object_persistent(
        GameCore::EntityId a_objectId,
        bool& a_outIsPersistent)
    {
        return m_gameWorld.is_object_persistent(a_objectId, a_outIsPersistent);
    }

    Result EngineCommandContext::set_object_persistent(
        GameCore::EntityId a_objectId,
        bool a_isPersistent)
    {
        return m_gameWorld.set_object_persistent(a_objectId, a_isPersistent);
    }

    Result EngineCommandContext::get_parent(GameCore::EntityId a_objectId, GameCore::EntityId& a_outParentId)
    {
        return m_gameWorld.get_parent(a_objectId, a_outParentId);
    }

    Result EngineCommandContext::set_parent(
        GameCore::EntityId a_objectId,
        GameCore::EntityId a_parentId,
        bool a_keepsWorldTransform)
    {
        if (a_parentId == GameCore::k_invalidEntityId)
        {
            return m_gameWorld.detach_parent(a_objectId, a_keepsWorldTransform);
        }

        return m_gameWorld.set_parent(a_objectId, a_parentId, a_keepsWorldTransform);
    }

    Result EngineCommandContext::get_transform_component(
        GameCore::EntityId a_objectId,
        ECS::TransformComponent& a_outComponent)
    {
        ECS::TransformComponent* component = nullptr;
        Result result = m_gameWorld.get_component(a_objectId, component);
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
        Result result = m_gameWorld.get_component(a_objectId, component);
        if (!result || component == nullptr)
        {
            return result;
        }

        *component = a_component;

        // local Transform を変えた frame で描画入力も追従するよう WorldTransform を再解決する。
        GameCore::TransformSystem::sync_world_transforms(m_gameWorld);
        return Result::ok();
    }

    Result EngineCommandContext::get_camera_component(
        GameCore::EntityId a_objectId,
        ECS::CameraComponent& a_outComponent)
    {
        ECS::CameraComponent* component = nullptr;
        Result result = m_gameWorld.get_component(a_objectId, component);
        if (!result || component == nullptr)
        {
            return result;
        }

        a_outComponent = *component;
        return Result::ok();
    }

    Result EngineCommandContext::set_camera_component(
        GameCore::EntityId a_objectId,
        const ECS::CameraComponent& a_component)
    {
        ECS::CameraComponent* component = nullptr;
        Result result = m_gameWorld.get_component(a_objectId, component);
        if (!result || component == nullptr)
        {
            return result;
        }

        *component = a_component;
        return Result::ok();
    }

    Result EngineCommandContext::get_mesh_filter_component(
        GameCore::EntityId a_objectId,
        ECS::MeshFilterComponent& a_outComponent)
    {
        ECS::MeshFilterComponent* component = nullptr;
        Result result = m_gameWorld.get_component(a_objectId, component);
        if (!result || component == nullptr)
        {
            return result;
        }

        a_outComponent = *component;
        return Result::ok();
    }

    Result EngineCommandContext::set_mesh_filter_component(
        GameCore::EntityId a_objectId,
        const ECS::MeshFilterComponent& a_component)
    {
        ECS::MeshFilterComponent* component = nullptr;
        Result result = m_gameWorld.get_component(a_objectId, component);
        if (!result || component == nullptr)
        {
            return result;
        }

        *component = a_component;
        return Result::ok();
    }

    Result EngineCommandContext::get_static_mesh_renderer_component(
        GameCore::EntityId a_objectId,
        ECS::StaticMeshRendererComponent& a_outComponent)
    {
        ECS::StaticMeshRendererComponent* component = nullptr;
        Result result = m_gameWorld.get_component(a_objectId, component);
        if (!result || component == nullptr)
        {
            return result;
        }

        a_outComponent = *component;
        return Result::ok();
    }

    Result EngineCommandContext::set_static_mesh_renderer_component(
        GameCore::EntityId a_objectId,
        const ECS::StaticMeshRendererComponent& a_component)
    {
        ECS::StaticMeshRendererComponent* component = nullptr;
        Result result = m_gameWorld.get_component(a_objectId, component);
        if (!result || component == nullptr)
        {
            return result;
        }

        *component = a_component;
        return Result::ok();
    }
} // namespace Cue
