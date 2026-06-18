#pragma once
#pragma once

/// ************************************************************************************
/// バッチングに関連するGPUデータ構造の定義
/// ************************************************************************************

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
        uint32_t objectId = 0;         // オブジェクトID
        uint32_t visible = 1;          // 可視フラグ（0: 非表示、1: 表示）
        uint32_t meshId = 0;           // メッシュID
        uint32_t transformId = 0;      // 変換ID
        uint32_t materialId = 0;       // マテリアルID
        uint32_t castsShadow = 1;      // 影を落とすか
        uint32_t receivesShadow = 1;   // 影を受けるか
        uint32_t shadowCasterMode = 0; // シャドウマップへの書き込み方式
        uint32_t skinPaletteOffset = UINT32_MAX;
        uint32_t skinPaletteCount = 0;
        uint32_t lodMeshId0 = 0;
        uint32_t lodMeshId1 = 0;
        uint32_t lodMeshId2 = 0;
        uint32_t lodMeshId3 = 0;
        uint32_t lodMeshId4 = 0;
        uint32_t lodCount = 1;
        uint32_t occluderMeshId = 0;
        uint32_t occluderFlags = 0;
        uint32_t padding0 = 0;
        uint32_t padding1 = 0;
        Math::float4 boundsCenterRadius = Math::float4(0.0f, 0.0f, 0.0f, 0.0f);
    };

    // 描画オブジェクト
    struct RenderObject
    {
        uint32_t objectId = 0;         // オブジェクトID
        uint32_t meshId = 0;           // メッシュID
        uint32_t transformId = 0;      // 変換ID
        uint32_t materialId = 0;       // マテリアルID
        uint32_t castsShadow = 1;      // 影を落とすか
        uint32_t receivesShadow = 1;   // 影を受けるか
        uint32_t shadowCasterMode = 0; // シャドウマップへの書き込み方式
        uint32_t skinPaletteOffset = UINT32_MAX;
        uint32_t skinPaletteCount = 0;
        uint32_t drawFlags = 0;
        uint32_t depthBin = 0;
        uint32_t padding = 0;
        Math::float4 boundsCenterRadius = Math::float4(0.0f, 0.0f, 0.0f, 0.0f);
    };

    struct RenderCellGpu
    {
        Math::float4 boundsCenterRadius = Math::float4(0.0f, 0.0f, 0.0f, 0.0f);
        uint32_t objectStart = 0;
        uint32_t objectCount = 0;
        uint32_t lodBias = 0;
        uint32_t flags = 0;
    };

    struct MaterialGpu
    {
        Math::float4 color = Math::float4(1.0f, 1.0f, 1.0f, 1.0f);
        uint32_t textureId = 0;
        uint32_t useTexture = 0;
        uint32_t useReflectionSkybox = 0;
        float shininess = 32.0f;
    };

    // インダイレクト描画コマンド
    struct IndirectCommand
    {
        uint32_t drawObjectStartIndex = 0;
        uint32_t indexCountPerInstance = 0;
        uint32_t instanceCount = 0;
        uint32_t startIndexLocation = 0;
        int32_t baseVertexLocation = 0;
        uint32_t startInstanceLocation = 0;
    };
} // namespace Cue::GpuData
