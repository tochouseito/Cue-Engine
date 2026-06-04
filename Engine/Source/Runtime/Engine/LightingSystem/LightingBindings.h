#pragma once

/// ****************************************************************************
/// Lighting buffer bindings used by draw passes
/// ****************************************************************************

// === RHI includes ===
#include <RHI.h>

namespace Cue::LightingSystem
{
    struct LightingBindings final
    {
        RHI::BufferHandle frameBuffer{};
        RHI::BufferHandle directionalLightBuffer{};
        RHI::BufferHandle pointLightBuffer{};
    };
}
