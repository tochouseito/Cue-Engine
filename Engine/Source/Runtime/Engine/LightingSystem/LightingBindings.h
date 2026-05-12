#pragma once

// === RHI includes ===
#include <RHI.h>

namespace Cue::LightingSystem
{
    struct LightingBindings final
    {
        RHI::BufferHandle frameBuffer{};
        RHI::BufferHandle directionalLightBuffer{};
        RHI::BufferHandle pointLightBuffer{};
        RHI::BufferHandle spotLightBuffer{};
    };
}
