#include "ScriptRegistry.h"

// === C++ includes ===
#include <array>

[[nodiscard]] Cue::Core::Native::ScriptClassDefinition
make_rotate_cube_script_definition() noexcept;

[[nodiscard]] Cue::Core::Native::ScriptClassDefinition
make_test_cube_script_definition() noexcept;

namespace
{
    const std::array<Cue::Core::Native::ScriptClassDefinition, 2> k_scriptClasses = {
        make_rotate_cube_script_definition(),
        make_test_cube_script_definition()
    };
}

std::span<const Cue::Core::Native::ScriptClassDefinition>
script_classes() noexcept
{
    return std::span<const Cue::Core::Native::ScriptClassDefinition>(
        k_scriptClasses.data(),
        k_scriptClasses.size());
}
