#include <Cue/RHI/PresentationContext.h>

#include <type_traits>
#include <utility>

static_assert(std::is_abstract_v<cue::PresentationContext>);
static_assert(std::has_virtual_destructor_v<cue::PresentationContext>);
static_assert(!std::is_copy_constructible_v<cue::PresentationContext>);

using PresentationFrameResult = decltype(std::declval<cue::PresentationContext &>().present_frame(
    std::declval<const cue::PresentationFrameDescriptor &>()));
static_assert(std::is_same_v<PresentationFrameResult, cue::Result<cue::PresentationFrameStatus>>);
