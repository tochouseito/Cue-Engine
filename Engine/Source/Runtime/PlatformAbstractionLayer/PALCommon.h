#pragma once

/// *********************************************************************************
/// PAL 共通ヘッダ
/// *********************************************************************************

// === C++ includes ===
#include <cstdint>

namespace Cue::PAL
{
    struct ProcessMemoryUsage
    {
        uint64_t workingSetBytes = 0;
        uint64_t privateBytes = 0;
    };

    struct SystemMemoryUsage
    {
        uint64_t totalPhysBytes = 0;
        uint64_t availPhysBytes = 0;
        uint32_t memoryLoadPercent = 0;
    };
}
