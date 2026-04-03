#pragma once

// === C++ includes ===
#include <cstdint>
#include <string>
#include <vector>

// === Math includes ===
#include <CueMath.h>

namespace Cue::GpuData
{
    // オブジェクト
    struct ObjectInfo
    {
        uint32_t objectId = 0;     // オブジェクトID
        uint32_t visible = 1;     // 可視フラグ（0: 非表示、1: 表示）
        uint32_t meshId = 0;      // メッシュID
        uint32_t transformId = 0; // 変換ID
    };

    // 描画オブジェクト
    struct RenderObject
    {
        uint32_t objectId = 0;     // オブジェクトID
        uint32_t meshId = 0;      // メッシュID
        uint32_t transformId = 0; // 変換ID
    };
}
