#pragma once

// === C++ Includes ===
#include <cstdint>

namespace Cue
{
    struct CueModelHeader final
    {
        uint32_t magic = 0;
        uint32_t version = 0;
        uint32_t meshCount = 0;
        uint32_t reserved = 0;
        uint64_t dataSize = 0;
    };

    struct CueModelMeshInfo final
    {
        uint32_t nameSize = 0;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        uint32_t flags = 0;
        uint64_t nameOffset = 0;
        uint64_t positionsOffset = 0;
        uint64_t uvsOffset = 0;
        uint64_t normalsOffset = 0;
        uint64_t indicesOffset = 0;
    };

    inline constexpr uint32_t k_cueModelMagic = 0x4d455543u;
    inline constexpr uint32_t k_cueModelVersion = 1u;
    inline constexpr uint32_t k_cueModelMeshFlagHasUvs = 0x1u;
    inline constexpr uint32_t k_cueModelMeshFlagHasNormals = 0x2u;
}
