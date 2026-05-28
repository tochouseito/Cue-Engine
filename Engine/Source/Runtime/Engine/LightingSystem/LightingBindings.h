// LightingBindings の役割と公開要素を定義する

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
