#include <Cue/RHI/PresentationContext.h>

#include <type_traits>

static_assert(std::is_abstract_v<cue::PresentationContext>);
static_assert(std::has_virtual_destructor_v<cue::PresentationContext>);
static_assert(!std::is_copy_constructible_v<cue::PresentationContext>);
