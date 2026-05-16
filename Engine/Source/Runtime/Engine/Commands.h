#pragma once

// === Base includes ===
#include <Result.h>

// === Core includes ===
#include <CQRS/CQRS.h>

// === Engine includes ===
#include "GameCore/Components.h"
#include "GameCore/GameCoreTypes.h"
#include "GameCore/SceneAsset.h"

// === C++ includes ===
#include <cstdint>
#include <string>
#include <string_view>

namespace Cue
{
    enum class AddableComponentType : uint8_t
    {
        Camera,
        MeshFilter,
        StaticMeshRenderer,
        SkinnedMeshRenderer,
        Animation,
        SpriteRenderer,
        AudioSource,
        RigidBody,
        Collider,
        CharacterController,
        DirectionalLight,
        PointLight,
        SpotLight,
        Script
    };

    enum class AddObjectType : uint8_t
    {
        GameObject,
        Camera,
        StaticMesh3D,
        Sprite2D,
        DirectionalLight,
        PointLight,
        SpotLight
    };

    class IGameCommandContext : public virtual Core::CQRS::ICommandContext
    {
    public:
        ~IGameCommandContext() override = default;

        virtual Result create_object(AddObjectType a_objectType,
            GameCore::EntityId& a_outObjectId) = 0;
        virtual Result create_object(
            AddObjectType a_objectType,
            GameCore::SceneId a_sceneId,
            GameCore::EntityId& a_outObjectId) = 0;
        virtual Result destroy_object(GameCore::EntityId a_objectId) = 0;
        virtual Result resolve_render_object_entity(
            uint32_t a_objectId, GameCore::EntityId& a_outEntityId) = 0;
        virtual Result set_main_camera(GameCore::EntityId a_cameraEntityId) = 0;
        virtual Result get_object_name(
            GameCore::EntityId a_objectId, std::string& a_outName) = 0;
        virtual Result rename_object(
            GameCore::EntityId a_objectId, std::string_view a_name) = 0;
        virtual Result capture_deleted_object(
            GameCore::EntityId a_objectId,
            GameCore::DeletedObjectSnapshot& a_outSnapshot) = 0;
        virtual Result restore_deleted_object(
            const GameCore::DeletedObjectSnapshot& a_snapshot,
            GameCore::EntityId& a_outObjectId) = 0;
        virtual Result add_component(
            GameCore::EntityId a_objectId,
            AddableComponentType a_componentType) = 0;
        virtual Result remove_component(
            GameCore::EntityId a_objectId,
            AddableComponentType a_componentType) = 0;
        virtual Result get_transform_component(GameCore::EntityId a_objectId,
            ECS::TransformComponent& a_outComponent) = 0;
        virtual Result set_transform_component(GameCore::EntityId a_objectId,
            const ECS::TransformComponent& a_component) = 0;
        virtual Result get_script_component(GameCore::EntityId a_objectId,
            ECS::ScriptComponent& a_outComponent) = 0;
        virtual Result set_script_component(GameCore::EntityId a_objectId,
            const ECS::ScriptComponent& a_component) = 0;
    };

    class AddObjectCommand final : public Core::CQRS::IUndoableCommand
    {
    public:
        explicit AddObjectCommand(
            AddObjectType a_objectType = AddObjectType::StaticMesh3D,
            GameCore::SceneId a_sceneId = GameCore::k_invalidSceneId) noexcept
            : m_objectType(a_objectType)
            , m_sceneId(a_sceneId)
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
                    "Command context does not support object creation.");
            }

            if (!m_hasSnapshot)
            {
                Result createResult =
                    gameCommandContext->create_object(
                        m_objectType, m_sceneId, m_objectId);
                if (!createResult)
                {
                    return createResult;
                }

                Result captureResult = gameCommandContext->capture_deleted_object(
                    m_objectId, m_snapshot);
                if (!captureResult)
                {
                    return captureResult;
                }

                m_hasSnapshot = true;
                return Result::ok();
            }

            return gameCommandContext->restore_deleted_object(
                m_snapshot, m_objectId);
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
                    "Command context does not support object creation undo.");
            }

            if (!m_hasSnapshot)
            {
                return Result::fail(
                    Code::InvalidState,
                    Severity::Error,
                    "Add object command snapshot was not captured.");
            }

            return gameCommandContext->destroy_object(m_objectId);
        }

    private:
        GameCore::EntityId m_objectId = GameCore::k_invalidEntityId;
        GameCore::DeletedObjectSnapshot m_snapshot{};
        AddObjectType m_objectType = AddObjectType::StaticMesh3D;
        GameCore::SceneId m_sceneId = GameCore::k_invalidSceneId;
        bool m_hasSnapshot = false;
    };

    class RemoveObjectCommand final : public Core::CQRS::IUndoableCommand
    {
    public:
        explicit RemoveObjectCommand(uint32_t a_objectId) noexcept
            : m_renderObjectId(a_objectId)
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
                    "Command context does not support object removal.");
            }

            if (!m_hasSnapshot)
            {
                Result resolveResult =
                    gameCommandContext->resolve_render_object_entity(
                        m_renderObjectId, m_objectId);
                if (!resolveResult)
                {
                    return resolveResult;
                }

                Result captureResult = gameCommandContext->capture_deleted_object(
                    m_objectId, m_snapshot);
                if (!captureResult)
                {
                    return captureResult;
                }

                m_hasSnapshot = true;
            }

            return gameCommandContext->destroy_object(m_objectId);
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
                    "Command context does not support object removal undo.");
            }

            if (!m_hasSnapshot)
            {
                return Result::fail(
                    Code::InvalidState,
                    Severity::Error,
                    "Remove object command snapshot was not captured.");
            }

            return gameCommandContext->restore_deleted_object(
                m_snapshot, m_objectId);
        }

    private:
        uint32_t m_renderObjectId = 0;
        GameCore::EntityId m_objectId = GameCore::k_invalidEntityId;
        GameCore::DeletedObjectSnapshot m_snapshot{};
        bool m_hasSnapshot = false;
    };

    class SetMainCameraCommand final : public Core::CQRS::ICommand
    {
    public:
        explicit SetMainCameraCommand(GameCore::EntityId a_cameraEntityId) noexcept
            : m_cameraEntityId(a_cameraEntityId)
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
                    "Command context does not support main camera switching.");
            }

            return gameCommandContext->set_main_camera(m_cameraEntityId);
        }

    private:
        GameCore::EntityId m_cameraEntityId = GameCore::k_invalidEntityId;
    };

    class RenameObjectCommand final : public Core::CQRS::IUndoableCommand
    {
    public:
        RenameObjectCommand(GameCore::EntityId a_objectId, std::string a_newName)
            : m_objectId(a_objectId), m_newName(std::move(a_newName))
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
                    "Command context does not support object rename.");
            }

            if (!m_hasCapturedOldName)
            {
                Result captureResult =
                    gameCommandContext->get_object_name(m_objectId, m_oldName);
                if (!captureResult)
                {
                    return captureResult;
                }

                m_hasCapturedOldName = true;
            }

            return gameCommandContext->rename_object(m_objectId, m_newName);
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
                    "Command context does not support object rename undo.");
            }

            if (!m_hasCapturedOldName)
            {
                return Result::fail(
                    Code::InvalidState,
                    Severity::Error,
                    "Rename command old name was not captured.");
            }

            return gameCommandContext->rename_object(m_objectId, m_oldName);
        }

    private:
        GameCore::EntityId m_objectId = GameCore::k_invalidEntityId;
        std::string m_oldName{};
        std::string m_newName{};
        bool m_hasCapturedOldName = false;
    };

    class DeleteObjectCommand final : public Core::CQRS::IUndoableCommand
    {
    public:
        explicit DeleteObjectCommand(GameCore::EntityId a_objectId) noexcept
            : m_objectId(a_objectId)
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
                    "Command context does not support object deletion.");
            }

            if (!m_hasSnapshot)
            {
                Result captureResult = gameCommandContext->capture_deleted_object(
                    m_objectId, m_snapshot);
                if (!captureResult)
                {
                    return captureResult;
                }

                m_hasSnapshot = true;
            }

            return gameCommandContext->destroy_object(m_objectId);
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
                    "Command context does not support object deletion undo.");
            }

            if (!m_hasSnapshot)
            {
                return Result::fail(
                    Code::InvalidState,
                    Severity::Error,
                    "Delete command snapshot was not captured.");
            }

            GameCore::EntityId restoredObjectId = GameCore::k_invalidEntityId;
            Result restoreResult = gameCommandContext->restore_deleted_object(
                m_snapshot, restoredObjectId);
            if (!restoreResult)
            {
                return restoreResult;
            }

            m_objectId = restoredObjectId;
            return Result::ok();
        }

    private:
        GameCore::EntityId m_objectId = GameCore::k_invalidEntityId;
        GameCore::DeletedObjectSnapshot m_snapshot{};
        bool m_hasSnapshot = false;
    };

    class AddComponentCommand final : public Core::CQRS::IUndoableCommand
    {
    public:
        AddComponentCommand(GameCore::EntityId a_objectId,
            AddableComponentType a_componentType) noexcept
            : m_objectId(a_objectId)
            , m_componentType(a_componentType)
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
                    "Command context does not support component addition.");
            }

            Result result =
                gameCommandContext->add_component(m_objectId, m_componentType);
            if (result)
            {
                m_hasAddedComponent = true;
            }

            return result;
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
                    "Command context does not support component addition undo.");
            }

            if (!m_hasAddedComponent)
            {
                return Result::fail(
                    Code::InvalidState,
                    Severity::Error,
                    "Add component command has not been executed.");
            }

            return gameCommandContext->remove_component(
                m_objectId, m_componentType);
        }

    private:
        GameCore::EntityId m_objectId = GameCore::k_invalidEntityId;
        AddableComponentType m_componentType = AddableComponentType::Camera;
        bool m_hasAddedComponent = false;
    };

    class RemoveComponentCommand final : public Core::CQRS::IUndoableCommand
    {
    public:
        RemoveComponentCommand(GameCore::EntityId a_objectId,
            AddableComponentType a_componentType) noexcept
            : m_objectId(a_objectId)
            , m_componentType(a_componentType)
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
                    "Command context does not support component removal.");
            }

            Result result =
                gameCommandContext->remove_component(m_objectId, m_componentType);
            if (result)
            {
                m_hasRemovedComponent = true;
            }

            return result;
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
                    "Command context does not support component removal undo.");
            }

            if (!m_hasRemovedComponent)
            {
                return Result::fail(
                    Code::InvalidState,
                    Severity::Error,
                    "Remove component command has not been executed.");
            }

            return gameCommandContext->add_component(
                m_objectId, m_componentType);
        }

    private:
        GameCore::EntityId m_objectId = GameCore::k_invalidEntityId;
        AddableComponentType m_componentType = AddableComponentType::Camera;
        bool m_hasRemovedComponent = false;
    };

    class SetTransformComponentCommand final : public Core::CQRS::IUndoableCommand
    {
    public:
        SetTransformComponentCommand(GameCore::EntityId a_objectId,
            ECS::TransformComponent a_newComponent) noexcept
            : m_objectId(a_objectId)
            , m_newComponent(std::move(a_newComponent))
        {
        }

        SetTransformComponentCommand(GameCore::EntityId a_objectId,
            ECS::TransformComponent a_oldComponent,
            ECS::TransformComponent a_newComponent) noexcept
            : m_objectId(a_objectId)
            , m_oldComponent(std::move(a_oldComponent))
            , m_newComponent(std::move(a_newComponent))
            , m_hasOldComponent(true)
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
                    "Command context does not support TransformComponent updates.");
            }

            if (!m_hasOldComponent)
            {
                Result captureResult =
                    gameCommandContext->get_transform_component(
                        m_objectId, m_oldComponent);
                if (!captureResult)
                {
                    return captureResult;
                }

                m_hasOldComponent = true;
            }

            return gameCommandContext->set_transform_component(
                m_objectId, m_newComponent);
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
                    "Command context does not support TransformComponent undo.");
            }

            if (!m_hasOldComponent)
            {
                return Result::fail(
                    Code::InvalidState,
                    Severity::Error,
                    "Set TransformComponent command has not been executed.");
            }

            return gameCommandContext->set_transform_component(
                m_objectId, m_oldComponent);
        }

    private:
        GameCore::EntityId m_objectId = GameCore::k_invalidEntityId;
        ECS::TransformComponent m_oldComponent{};
        ECS::TransformComponent m_newComponent{};
        bool m_hasOldComponent = false;
    };

    class SetScriptComponentCommand final : public Core::CQRS::IUndoableCommand
    {
    public:
        SetScriptComponentCommand(GameCore::EntityId a_objectId,
            ECS::ScriptComponent a_newComponent) noexcept
            : m_objectId(a_objectId)
            , m_newComponent(std::move(a_newComponent))
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
                    "Command context does not support ScriptComponent updates.");
            }

            if (!m_hasOldComponent)
            {
                Result captureResult =
                    gameCommandContext->get_script_component(
                        m_objectId, m_oldComponent);
                if (!captureResult)
                {
                    return captureResult;
                }

                m_hasOldComponent = true;
            }

            return gameCommandContext->set_script_component(
                m_objectId, m_newComponent);
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
                    "Command context does not support ScriptComponent undo.");
            }

            if (!m_hasOldComponent)
            {
                return Result::fail(
                    Code::InvalidState,
                    Severity::Error,
                    "Set ScriptComponent command has not been executed.");
            }

            return gameCommandContext->set_script_component(
                m_objectId, m_oldComponent);
        }

    private:
        GameCore::EntityId m_objectId = GameCore::k_invalidEntityId;
        ECS::ScriptComponent m_oldComponent{};
        ECS::ScriptComponent m_newComponent{};
        bool m_hasOldComponent = false;
    };
} // namespace Cue
