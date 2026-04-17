#pragma once

#include <Native/ScriptModuleRuntime.h>

// *** Script classes registered in this module
[[nodiscard]] std::span<const Cue::Core::Native::ScriptClassDefinition>
script_classes() noexcept;
