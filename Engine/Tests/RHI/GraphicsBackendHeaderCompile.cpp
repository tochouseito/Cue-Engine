#include <Cue/RHI/GraphicsBackend.h>

#include <memory>
#include <type_traits>

static_assert(std::is_abstract_v<cue::GraphicsBackend>);
static_assert(std::has_virtual_destructor_v<cue::GraphicsBackend>);
static_assert(!std::is_copy_constructible_v<cue::GraphicsBackend>);
static_assert(std::is_same_v<std::unique_ptr<cue::GraphicsBackend>::element_type, cue::GraphicsBackend>);
