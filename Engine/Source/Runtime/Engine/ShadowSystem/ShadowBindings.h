#pragma once

// === RHI includes ===
#include <RHICommon.h>

namespace Cue::ShadowSystem
{
    struct ShadowBindings final
    {
        RHI::BufferHandle spotShadowFrameBuffer{};
    };
}
