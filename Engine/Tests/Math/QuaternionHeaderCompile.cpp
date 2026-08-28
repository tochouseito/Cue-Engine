#include <Cue/Math/Quaternion.h>

#include <type_traits>

static_assert(sizeof(cue::math::Quaternion) == sizeof(float) * 4);
static_assert(std::is_standard_layout_v<cue::math::Quaternion>);
static_assert(std::is_trivially_copyable_v<cue::math::Quaternion>);
