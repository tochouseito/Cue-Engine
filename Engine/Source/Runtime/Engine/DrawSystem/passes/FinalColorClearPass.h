#pragma once

/// ************************************************************************************
/// FinalColor clear pass
/// ************************************************************************************

// === RHI includes ===
#include "FrameGraph.h"

namespace Cue::RHI
{
    class FinalColorClearPass final : public FrameGraphPass
    {
    public:
        const char* name() const noexcept override
        {
            return "FinalColorClear";
        }

        CommandListType type() const noexcept override
        {
            return CommandListType::Graphics;
        }

        Result setup(FrameGraphBuilder& builder) override
        {
            Result result =
                builder.get_texture("FinalColor", m_finalColorHandle);
            if (!result)
            {
                return Result::fail(
                    Code::GetFailed, Severity::Error,
                    "Failed to get FinalColor texture handle for clear pass.");
            }

            result = builder.get_view("FinalColorRTV", m_finalColorRtvHandle);
            if (!result)
            {
                return Result::fail(
                    Code::GetFailed, Severity::Error,
                    "Failed to get FinalColor RTV handle for clear pass.");
            }

            return Result::ok();
        }

        Result describe_resources(FrameGraphBuilder& builder) override
        {
            return builder.use_texture(
                m_finalColorHandle, ResourceAccessType::Write,
                ResourceState::RenderTarget, ResourceState::Common);
        }

        void execute(FrameGraphContext& context) override
        {
            ICommandContext* commandContext = context.commandContext();
            commandContext->clear_render_target(m_finalColorRtvHandle,
                                                k_clearColor.data());
        }

    private:
        static constexpr Math::float4 k_clearColor =
            Math::float4::from_rgba8(0, 0, 0, 255);

        TextureHandle m_finalColorHandle{};
        ViewHandle m_finalColorRtvHandle{};
    };
} // namespace Cue::RHI
