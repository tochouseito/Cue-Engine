#include <Cue/Math/Scalar.h>

#include <type_traits>

static_assert(std::is_standard_layout_v<cue::math::Tolerance>);
static_assert(std::is_nothrow_move_constructible_v<cue::math::Tolerance>);
