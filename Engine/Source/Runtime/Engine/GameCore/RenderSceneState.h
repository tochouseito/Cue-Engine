#pragma once

// === C++ includes ===
#include <vector>

namespace Cue
{
    struct RenderFrameState final
    {
        uint32_t objectCount = 0;
    };

    struct RenderSceneState final
    {
        void resize(const uint32_t a_bufferCount)
        {
            frameStates.resize(a_bufferCount);
        }

        RenderFrameState& frame_state(const uint32_t a_bufferIndex) noexcept
        {
            return frameStates[a_bufferIndex];
        }

        const RenderFrameState& frame_state(const uint32_t a_bufferIndex) const noexcept
        {
            return frameStates[a_bufferIndex];
        }

        std::vector<RenderFrameState> frameStates{};
    };
} // namespace Cue
