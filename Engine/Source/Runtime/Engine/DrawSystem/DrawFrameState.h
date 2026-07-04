#pragma once

/// ****************************************************************************
/// フレームごとの描画状態を管理する構造体定義
/// ****************************************************************************

// === Math includes ===
#include <CueMath.h>

// === Engine includes ===
#include "DrawSystem/StaticMeshBatch.h"

// === C++ includes ===
#include <vector>

namespace Cue::DrawSystem
{
    struct CpuIndexedDraw final
    {
        uint32_t renderObjectId = 0;
        uint32_t indexCount = 0;
        uint32_t startIndex = 0;
        int32_t baseVertex = 0;
        float sortDepth = 0.0f;
    };

    struct DrawFrameData final
    {
        uint32_t objectCount = 0;
        uint32_t particleCount = 0;
        uint32_t cellCount = 0;
        uint32_t staticMeshBatchCount = 0;
        uint32_t indirectCommandCount = 0;
        uint32_t renderWidth = 1;
        uint32_t renderHeight = 1;
        bool useCpuBatching = false;
        std::vector<CpuIndexedDraw> cpuIndexedDraws{};
        std::vector<CpuIndexedDraw> transparentCpuIndexedDraws{};
        std::vector<StaticMeshBatch> staticMeshBatches{};
        std::vector<GpuData::IndirectCommand> staticMeshIndirectCommands{};
        std::vector<uint32_t> staticMeshObjectIndices{};
    };

    struct DrawFrameState final
    {
        void resize(const uint32_t a_bufferCount)
        {
            frameStates.resize(a_bufferCount);
        }

        DrawFrameData& frame_state(const uint32_t a_bufferIndex) noexcept
        {
            return frameStates[a_bufferIndex];
        }

        const DrawFrameData& frame_state(const uint32_t a_bufferIndex) const noexcept
        {
            return frameStates[a_bufferIndex];
        }

        std::vector<DrawFrameData> frameStates{};
    };
} // namespace Cue::DrawSystem
