#pragma once

// === C++ includes ===
#include <array>
#include <cstdint>

// === Math includes ===
#include <CueMath.h>

namespace Cue::GpuData
{
    inline constexpr uint32_t k_maxDebugSelectionItemCount = 64;

    enum class DebugSelectionShape : uint32_t
    {
        Box = 0,
        CameraFrustum = 1,
        Line = 2,
    };

    struct DebugSelectionItemGpu final
    {
        Math::float4x4 world;
        Math::float4 color = Math::float4(1.0f, 0.84f, 0.18f, 1.0f);
        Math::float4 camera =
            Math::float4(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
        uint32_t shape = static_cast<uint32_t>(DebugSelectionShape::Box);
        uint32_t isEnabled = 0;
        uint32_t padding0 = 0;
        uint32_t padding1 = 0;
    };

    struct DebugSelectionGpu final
    {
        uint32_t itemCount = 0;
        uint32_t padding0 = 0;
        uint32_t padding1 = 0;
        uint32_t padding2 = 0;
        std::array<DebugSelectionItemGpu, k_maxDebugSelectionItemCount> items{};
    };
}
