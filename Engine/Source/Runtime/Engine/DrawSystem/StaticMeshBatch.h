#pragma once

/// ************************************************************************************
/// StaticMesh の CPU バッチング結果
/// ************************************************************************************

// === Engine includes ===
#include "DrawSystem/GpuData/Batching.h"
#include "DrawSystem/MeshPool.h"

// === C++ includes ===
#include <cstdint>
#include <vector>

namespace Cue::DrawSystem
{
    /// @brief StaticMesh を同じ draw call にまとめるための分類キー
    struct StaticMeshBatchKey final
    {
        uint32_t meshId = 0;     // MeshPool 上の mesh ID
        uint32_t materialId = 0; // Material buffer 上の material ID
    };

    /// @brief 1 回の DrawIndexedInstancedIndirect に対応する CPU 側バッチ
    struct StaticMeshBatch final
    {
        StaticMeshBatchKey key{}; // このバッチに含まれる mesh/material の組み合わせ
        MeshRange meshRange{};    // MeshPool から取得した DrawIndexed 用範囲
        uint32_t firstObjectIndex = 0; // groupedObjectIndices 内の開始 index
        uint32_t objectCount = 0;      // このバッチに含まれる object 数
    };

    /// @brief StaticMesh batching の CPU 側生成結果
    struct StaticMeshBatchBuildResult final
    {
        std::vector<StaticMeshBatch> batches{};              // CPU 側で参照するバッチ一覧
        std::vector<GpuData::IndirectCommand> commands{};    // indirect draw に渡すコマンド一覧
        std::vector<uint32_t> groupedObjectIndices{};        // バッチ順に並べた DrawScene object index
    };
} // namespace Cue::DrawSystem
