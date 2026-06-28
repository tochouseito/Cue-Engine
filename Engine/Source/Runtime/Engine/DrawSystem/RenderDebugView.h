#pragma once

#include <cstdint>

namespace Cue::DrawSystem {

enum class RenderDebugView : uint32_t {
  Forward = 0,
  VisibilityObjectId = 1,
  VisibilityPrimitiveId = 2,
  VisibilityBarycentric = 3,
  VisibilityNormal = 4,
  VisibilityUv = 5,
  VisibilityLit = 6,
  VisibilityMaterial = 7,
};

} // namespace Cue::DrawSystem
