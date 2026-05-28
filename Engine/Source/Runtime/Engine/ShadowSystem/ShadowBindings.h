// ShadowBindings の役割と公開要素を定義する

#pragma once

// === RHI includes ===
#include <RHICommon.h>

namespace Cue::ShadowSystem
{
    struct ShadowBindings final
    {
        RHI::BufferHandle directionalShadowFrameBuffer{};
        RHI::BufferHandle pointShadowFaceBuffer{};
        RHI::BufferHandle spotShadowFrameBuffer{};
    };
}
