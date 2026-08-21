#include <Cue/Foundation/Result.h>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<cue::Result<int>>);
static_assert(std::is_nothrow_move_constructible_v<cue::Result<int>>);
