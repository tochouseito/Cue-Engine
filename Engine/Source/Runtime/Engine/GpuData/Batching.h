#pragma once

// === C++ includes ===
#include <cstdint>
#include <string>
#include <vector>

// === Math includes ===
#include <CueMath.h>

namespace Cue::GpuData
{
    // 描画可能オブジェクト
    struct RenderableInfo
    {
        uint32_t objectId = 0;      // オブジェクトID
        uint32_t visible = 1;       // 可視フラグ（0: 非表示、1: 表示）
        uint32_t meshId = 0;        // メッシュID
        uint32_t transformId = 0;   // 変換ID
        uint32_t materialId = 0;    // マテリアルID
    };

    // 描画オブジェクト
    struct RenderObject
    {
        uint32_t objectId = 0;      // オブジェクトID
        uint32_t meshId = 0;        // メッシュID
        uint32_t transformId = 0;   // 変換ID
        uint32_t materialId = 0;    // マテリアルID
    };

    struct MaterialGpu
    {
        Math::float4 color = Math::float4(1.0f, 1.0f, 1.0f, 1.0f);
        uint32_t textureId = 0;
        uint32_t padding0 = 0;
        uint32_t padding1 = 0;
        uint32_t padding2 = 0;
    };

    // インダイレクト描画コマンド
    struct IndirectCommand
    {
        uint32_t indexCountPerInstance = 0;
        uint32_t instanceCount = 0;
        uint32_t startIndexLocation = 0;
        int32_t baseVertexLocation = 0;
        uint32_t startInstanceLocation = 0;
    };
}
