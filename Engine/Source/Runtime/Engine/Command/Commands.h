#pragma once

/// **********************************************************************
/// Command 定義
/// **********************************************************************

// === Base includes ===
#include <CueResult.h>

// === Core includes ===
#include <CQRS/CQRS.h>

// === GameCore includes ===
#include <GameCore/GameCoreTypes.h>

// === C++ includes ===
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace Cue
{
    class IGameCommandContext : public virtual Core::CQRS::ICommandContext
    {
    public:
        ~IGameCommandContext() override = default;

        virtual Result destroy_object(GameCore::EntityId a_objectId) = 0;
        virtual Result get_object_name(GameCore::EntityId a_objectId, std::string& a_outName) = 0;
        virtual Result rename_object(GameCore::EntityId a_objectId, std::string_view a_name) = 0;
        virtual Result get_parent(GameCore::EntityId a_objectId, GameCore::EntityId& a_outParentId) = 0;
        virtual Result set_parent(
            GameCore::EntityId a_objectId,
            GameCore::EntityId a_parentId,
            bool a_keepsWorldTransform) = 0;
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

    class DeleteObjectCommand final : public Core::CQRS::ICommand
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

            return gameCommandContext->destroy_object(m_objectId);
        }

    private:
        GameCore::EntityId m_objectId = GameCore::k_invalidEntityId;
    };

    class SetParentCommand final : public Core::CQRS::IUndoableCommand
    {
    public:
        SetParentCommand(
            GameCore::EntityId a_objectId,
            GameCore::EntityId a_newParentId,
            bool a_keepsWorldTransform = true) noexcept
            : m_objectId(a_objectId)
            , m_newParentId(a_newParentId)
            , m_keepsWorldTransform(a_keepsWorldTransform)
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
                    "Command context does not support parent updates.");
            }

            if (!m_hasOldParent)
            {
                Result captureResult =
                    gameCommandContext->get_parent(m_objectId, m_oldParentId);
                if (!captureResult)
                {
                    return captureResult;
                }

                m_hasOldParent = true;
            }

            return gameCommandContext->set_parent(
                m_objectId, m_newParentId, m_keepsWorldTransform);
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
                    "Command context does not support parent update undo.");
            }

            if (!m_hasOldParent)
            {
                return Result::fail(
                    Code::InvalidState,
                    Severity::Error,
                    "Set parent command has not been executed.");
            }

            return gameCommandContext->set_parent(
                m_objectId, m_oldParentId, m_keepsWorldTransform);
        }

    private:
        GameCore::EntityId m_objectId = GameCore::k_invalidEntityId;
        GameCore::EntityId m_oldParentId = GameCore::k_invalidEntityId;
        GameCore::EntityId m_newParentId = GameCore::k_invalidEntityId;
        bool m_keepsWorldTransform = true;
        bool m_hasOldParent = false;
    };
}
