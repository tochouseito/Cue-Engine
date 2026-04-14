#pragma once

// === Base includes ===
#include <Result.h>

// === Core includes ===
#include <CQRS/CQRS.h>

// === Engine includes ===
#include "GameCore/GameCoreTypes.h"
#include "GameCore/SceneAsset.h"

// === C++ includes ===
#include <cstdint>
#include <string>
#include <string_view>

namespace Cue
{
    class IGameCommandContext : public virtual Core::CQRS::ICommandContext
    {
    public:
        ~IGameCommandContext() override = default;

        virtual Result create_object(GameCore::EntityId& a_outObjectId) = 0;
        virtual Result destroy_object(GameCore::EntityId a_objectId) = 0;
        virtual Result resolve_render_object_entity(
            uint32_t a_objectId, GameCore::EntityId& a_outEntityId) = 0;
        virtual Result set_main_camera(uint32_t a_cameraIndex) = 0;
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
    };

    class AddObjectCommand final : public Core::CQRS::IUndoableCommand
    {
    public:
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
                Result createResult = gameCommandContext->create_object(m_objectId);
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
        explicit SetMainCameraCommand(uint32_t a_cameraIndex) noexcept
            : m_cameraIndex(a_cameraIndex)
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

            return gameCommandContext->set_main_camera(m_cameraIndex);
        }

    private:
        uint32_t m_cameraIndex = 0;
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
} // namespace Cue
