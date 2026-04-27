#pragma once

// === C++ includes ===
#include <cstdint>
#include <string>
#include <vector>

// === Math includes ===
#include <CueMath.h>

namespace Cue::GpuData
{
    // ローカル空間の変換
    struct LocalTransform
    {
        Math::float3 position{ 0.0f, 0.0f, 0.0f }; // ローカル位置
        Math::float3 rotation{ 0.0f, 0.0f, 0.0f }; // ローカル回転（オイラー角）
        Math::float3 scale{ 1.0f, 1.0f, 1.0f };    // ローカルスケール
    };

    // GPU ワールド変換行列
    struct ObjectTransformGpu
    {
        Math::float4x4 worldMatrix; // ワールド変換行列
        Math::float4x4 normalMatrix; // 法線変換行列
    };
}
