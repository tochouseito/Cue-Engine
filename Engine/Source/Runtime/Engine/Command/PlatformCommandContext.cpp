#include "PlatformCommandContext.h"

namespace Cue
{
    PlatformCommandContext::PlatformCommandContext(PAL::PlatformRuntimeState& a_state, FrameController* a_frameController) noexcept
        : m_state(a_state), m_frameController(a_frameController)
    {}

    Result PlatformCommandContext::request_window_resize(
        uint32_t a_width,
        uint32_t a_height)
    {
        Result result = m_state.request_window_resize(a_width, a_height);
        if (!result)
        {
            return result;
        }

        if (m_frameController != nullptr)
        {
            m_frameController->poll_resize_request();
        }

        return Result::ok();
    }
}
