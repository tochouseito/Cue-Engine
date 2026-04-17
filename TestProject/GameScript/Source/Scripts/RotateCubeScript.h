#pragma once

#include <Framework/Marionette.h>

MARIONETTE_DECLARE_SCRIPT_TYPE(RotateCube, "RotateCube");

[[nodiscard]] Cue::Core::Native::ScriptClassDefinition
make_rotate_cube_script_definition() noexcept;
