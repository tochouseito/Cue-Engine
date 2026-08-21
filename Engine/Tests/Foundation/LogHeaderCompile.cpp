#include <Cue/Foundation/Log.h>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<cue::LogRecord>);
static_assert(!std::is_copy_constructible_v<cue::Logger>);
