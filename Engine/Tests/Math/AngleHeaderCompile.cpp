#include <Cue/Math/Angle.h>

#include <type_traits>

static_assert(std::is_standard_layout_v<cue::math::Radians>);
static_assert(std::is_trivially_copyable_v<cue::math::Radians>);
static_assert(std::is_standard_layout_v<cue::math::Degrees>);
static_assert(std::is_trivially_copyable_v<cue::math::Degrees>);
