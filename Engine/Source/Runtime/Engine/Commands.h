#pragma once

// === Base includes ===
#include <Result.h>

// === Core includes ===
#include <CQRS/CQRS.h>

// === C++ includes ===
#include <cstdint>

namespace Cue
{
    class IGameCommandContext : public virtual Core::CQRS::ICommandContext
    {
    public:
        ~IGameCommandContext() override = default;

        virtual Result add_object() = 0;
        virtual Result remove_object(uint32_t a_objectId) = 0;
    };

    class AddObjectCommand final : public Core::CQRS::ICommand
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

            return gameCommandContext->add_object();
        }
    };

    class RemoveObjectCommand final : public Core::CQRS::ICommand
    {
    public:
        explicit RemoveObjectCommand(uint32_t a_objectId) noexcept
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
                    "Command context does not support object removal.");
            }

            return gameCommandContext->remove_object(m_objectId);
        }

    private:
        uint32_t m_objectId = 0;
    };
} // namespace Cue
