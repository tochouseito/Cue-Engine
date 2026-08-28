#include <Cue/Math/Transform.h>

#include <type_traits>

static_assert(std::is_standard_layout_v<cue::math::Transform>);
static_assert(std::is_trivially_copyable_v<cue::math::Transform>);
