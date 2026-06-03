#pragma once

/// ************************************************************************************
/// ビュー行列とプロジェクション行列のGPUデータ構造の定義
/// ************************************************************************************

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
        Math::float4 cameraPosition = Math::float4(0.0f, 0.0f, 0.0f, 1.0f);
    };
}
