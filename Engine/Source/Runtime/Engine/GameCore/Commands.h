#pragma once

// === Base includes ===
#include <Result.h>

// === Core includes ===
#include <CQRS/CQRS.h>

namespace Cue
{
    class IGameCommandContext : public Core::CQRS::ICommandContext
    {
    public:
        ~IGameCommandContext() override = default;

        virtual Result add_object() = 0;
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
                return Result::fail(Code::InvalidArgument, Severity::Error,
                    "Command context does not support object creation.");
            }

            return gameCommandContext->add_object();
        }
    };
} // namespace Cue
