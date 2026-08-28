#include <Cue/Math/Vector.h>

#include <type_traits>

static_assert(sizeof(cue::math::Vector2) == sizeof(float) * 2);
static_assert(sizeof(cue::math::Vector3) == sizeof(float) * 3);
static_assert(sizeof(cue::math::Vector4) == sizeof(float) * 4);
static_assert(std::is_standard_layout_v<cue::math::Vector2>);
static_assert(std::is_standard_layout_v<cue::math::Vector3>);
static_assert(std::is_standard_layout_v<cue::math::Vector4>);
static_assert(std::is_trivially_copyable_v<cue::math::Vector2>);
static_assert(std::is_trivially_copyable_v<cue::math::Vector3>);
static_assert(std::is_trivially_copyable_v<cue::math::Vector4>);
