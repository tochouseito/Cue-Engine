#include <Cue/Math/Matrix.h>

#include <type_traits>

static_assert(sizeof(cue::math::Matrix3) == sizeof(float) * 9);
static_assert(sizeof(cue::math::Matrix4) == sizeof(float) * 16);
static_assert(std::is_standard_layout_v<cue::math::Matrix3>);
static_assert(std::is_standard_layout_v<cue::math::Matrix4>);
static_assert(std::is_trivially_copyable_v<cue::math::Matrix3>);
static_assert(std::is_trivially_copyable_v<cue::math::Matrix4>);
