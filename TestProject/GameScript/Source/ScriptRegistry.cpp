#include "ScriptRegistry.h"

#include "Scripts/RotateCubeScript.h"

// === C++ includes ===
#include <array>

namespace
{
    const std::array<Cue::Core::Native::ScriptClassDefinition, 1> k_scriptClasses = {
        make_rotate_cube_script_definition()
    };
}

std::span<const Cue::Core::Native::ScriptClassDefinition>
script_classes() noexcept
{
    return std::span<const Cue::Core::Native::ScriptClassDefinition>(
        k_scriptClasses.data(),
        k_scriptClasses.size());
}
