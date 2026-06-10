#pragma once

/// ************************************************************************************
/// FinalColor をクリアするテスト用のPass
/// ************************************************************************************

// === RHI includes ===
#include "FrameGraph.h"

// === C++ includes ===
#include <array>

namespace Cue::DrawSystem
{
    class FinalColorClearPass final : public RHI::FrameGraphPass
    {
      public:
        const char* name() const noexcept override
        {
            return "FinalColorClear";
        }
        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Graphics;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            Result result =
                builder.get_texture("FinalColor", m_finalColorHandle);
            if (!result)
            {
                return Result::fail(
                    result.code, Severity::Error,
                    "Failed to get final color texture handle for clear pass.");
            }

            result = builder.render(&m_finalColorHandle, 1);
            if (!result)
            {
                return Result::fail(result.code, Severity::Error,
                                    "Failed to declare final color as render "
                                    "target for clear pass.");
            }

            result = builder.get_view("FinalColorRTV", m_finalColorRtvHandle);
            if (!result)
            {
                return Result::fail(result.code, Severity::Error,
                                    "Failed to get final color RTV view handle "
                                    "for clear pass.");
            }

            return Result::ok();
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            return builder.use_texture(m_finalColorHandle,
                                       RHI::ResourceAccessType::Write,
                                       RHI::ResourceState::RenderTarget,
                                       RHI::ResourceState::RenderTarget);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            commandContext->clear_render_target(m_finalColorRtvHandle,
                                                k_clearColor.data());
        }

      private:
        static constexpr std::array<float, 4> k_clearColor = {
            0.247058824f,
            0.247058824f,
            0.247058824f,
            1.0f,
        };

        RHI::TextureHandle m_finalColorHandle{};
        RHI::ViewHandle m_finalColorRtvHandle{};
    };
} // namespace Cue::DrawSystem
