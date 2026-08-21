#include <Cue/Foundation/EmergencyHandler.h>

#include <type_traits>

static_assert(std::has_virtual_destructor_v<cue::EmergencyHandler>);
