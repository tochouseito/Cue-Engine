#include <Cue/Foundation/Error.h>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<cue::Error>);
static_assert(std::is_nothrow_move_constructible_v<cue::Error>);
