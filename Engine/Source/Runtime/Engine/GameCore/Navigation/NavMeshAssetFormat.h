// NavMeshAssetFormat の役割と公開要素を定義する

#pragma once

// === C++ includes ===
#include <cstdint>

namespace Cue::GameCore
{
    struct CueNavMeshHeader final
    {
        uint32_t magic = 0;
        uint32_t version = 0;
        uint32_t flags = 0;
        uint32_t tileCount = 0;
        uint64_t dataSize = 0;
        uint64_t sourceGeometryHash = 0;
        uint64_t buildHash = 0;
        float cellSize = 0.0f;
        float cellHeight = 0.0f;
        float agentHeight = 0.0f;
        float agentRadius = 0.0f;
        float agentMaxClimb = 0.0f;
        float agentMaxSlope = 0.0f;
        float regionMinSize = 0.0f;
        float regionMergeSize = 0.0f;
        float edgeMaxLen = 0.0f;
        float edgeMaxError = 0.0f;
        float detailSampleDist = 0.0f;
        float detailSampleMaxError = 0.0f;
        int32_t vertsPerPoly = 0;
        uint32_t reserved = 0;
    };

    struct CueNavMeshTileInfo final
    {
        int32_t tileX = 0;
        int32_t tileY = 0;
        int32_t layer = 0;
        uint32_t flags = 0;
        uint64_t dataOffset = 0;
        uint64_t dataSize = 0;
    };

    inline constexpr uint32_t k_cueNavMeshMagic = 0x564E5543u;
    inline constexpr uint32_t k_cueNavMeshVersion = 1u;
    inline constexpr uint32_t k_cueNavMeshFlagTiled = 0x1u;
}
