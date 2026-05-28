// DebugViewShadingMode の役割と公開要素を定義する

#pragma once

// === C++ includes ===
#include <cstdint>

namespace Cue::DrawSystem
{
    enum class DebugViewShadingMode : uint32_t
    {
        Solid = 0,
        Material = 1,
        Lighting = 2,
        MaterialLighting = 3,
    };

    [[nodiscard]] constexpr uint32_t to_shader_value(
        DebugViewShadingMode a_mode) noexcept
    {
        return static_cast<uint32_t>(a_mode);
    }
} // namespace Cue::DrawSystem
