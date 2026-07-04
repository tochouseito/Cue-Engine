#pragma once

/// ************************************************************************************
/// エフェクト描画に関連する GPU データ構造の定義
/// ************************************************************************************

// === Math includes ===
#include <CueMath.h>

namespace Cue::GpuData
{
    struct ParticleSpriteGpu final
    {
        Math::float4 positionSize = Math::float4(0.0f, 0.0f, 0.0f, 1.0f);
        Math::float4 color = Math::float4(1.0f, 1.0f, 1.0f, 1.0f);
    };
} // namespace Cue::GpuData
