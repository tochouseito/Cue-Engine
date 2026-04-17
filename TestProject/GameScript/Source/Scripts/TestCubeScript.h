#pragma once

#include <Framework/Marionette.h>

MARIONETTE_DECLARE_SCRIPT_TYPE(TestCube, "TestCube");

[[nodiscard]] Cue::Core::Native::ScriptClassDefinition
make_test_cube_script_definition() noexcept;
