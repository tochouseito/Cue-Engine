#pragma once

#include <cstdint>

namespace Cue::DrawSystem {

enum class RenderPath : uint32_t {
  Forward = 0,
  VisibilityBuffer = 1,
};

} // namespace Cue::DrawSystem
