#pragma once

/// ************************************************************************************
/// StaticMesh の CPU バッチ生成
/// ************************************************************************************

// === Base includes ===
#include <CueResult.h>

// === Engine includes ===
#include "DrawSystem/StaticMeshBatch.h"

namespace Cue::DrawSystem
{
    class DrawScene;
    class MeshPool;

    namespace StaticMeshBatcher
    {
        /// @brief StaticMeshDrawObject を mesh/material 単位にまとめ、CPU 上で indirect command を作る
        [[nodiscard]] Result build_indirect_commands(
            const DrawScene& a_scene,
            const MeshPool& a_meshPool,
            StaticMeshBatchBuildResult& a_outResult);
    }
} // namespace Cue::DrawSystem
