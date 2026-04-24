#pragma once

// === C++ includes ===
#include <cstdint>
#include <string>
#include <vector>

// === Math includes ===
#include <CueMath.h>

namespace Cue::GpuData
{
    struct ViewProjectionGpu final
    {
        Math::float4x4 view;       // ビュー行列
        Math::float4x4 projection; // プロジェクション行列
    };
}
