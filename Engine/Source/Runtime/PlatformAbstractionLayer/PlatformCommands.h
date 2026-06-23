#pragma once

/// ********************************************************************************
/// プラットフォームコマンド
/// ********************************************************************************

// === Base includes ===
#include <CueResult.h>

// === Core includes ===
#include <CQRS/CQRS.h>

// === C++ includes ===
#include <cstdint>

namespace Cue::PAL
{
    /// @brief Command インタフェース
    class IPlatformCommandContext : public virtual Core::CQRS::ICommandContext
    {
    public:
        ~IPlatformCommandContext() override = default;

        virtual Result request_window_resize(uint32_t a_width, uint32_t a_height) = 0;
    };

    /// @brief ウィンドウリサイズコマンド
    class ResizeWindowCommand final : public Core::CQRS::ICommand
    {
    public:
        ResizeWindowCommand(uint32_t a_width, uint32_t a_height) noexcept
            : m_width(a_width)
            , m_height(a_height)
        {}

        Result execute(Core::CQRS::ICommandContext& a_commandContext) override
        {
            IPlatformCommandContext* platformCommandContext =
                dynamic_cast<IPlatformCommandContext*>(&a_commandContext);
            if (platformCommandContext == nullptr)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Command context does not support window resize requests.");
            }

            return platformCommandContext->request_window_resize(
                m_width, m_height);
        }

    private:
        uint32_t m_width = 0;
        uint32_t m_height = 0;
    };
}
