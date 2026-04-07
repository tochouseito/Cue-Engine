#pragma once

// === Engine includes ===
#include <GpuData/Batching.h>
#include <GpuData/Transform.h>

// === C++ includes ===
#include <cstdint>
#include <vector>

namespace Cue
{
    struct RenderFrameState final
    {
        uint32_t objectCount = 0;
    };

    struct RenderSceneState final
    {
        RenderFrameState frameState{};
        std::vector<GpuData::ObjectInfo> objectInfos{};
        std::vector<GpuData::LocalTransform> localTransforms{};
    };
} // namespace Cue
