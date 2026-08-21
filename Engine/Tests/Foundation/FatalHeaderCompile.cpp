#include <Cue/Foundation/Fatal.h>

#include <type_traits>

static_assert(std::is_base_of_v<cue::EmergencyHandler, cue::FatalHandler>);
