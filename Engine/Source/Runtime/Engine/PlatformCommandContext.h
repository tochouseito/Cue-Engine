#pragma once

// === Base includes ===
#include <Result.h>

// === PAL includes ===
#include <PlatformCommands.h>
#include <PlatformRuntimeState.h>

namespace Cue
{
    class FrameController;

    class PlatformCommandContext final : public PAL::IPlatformCommandContext
    {
    public:
        PlatformCommandContext(PAL::PlatformRuntimeState& a_state,
            FrameController* a_frameController) noexcept;

        Result request_window_resize(uint32_t a_width, uint32_t a_height) override;

    private:
        PAL::PlatformRuntimeState& m_state;
        FrameController* m_frameController = nullptr;
    };
}
