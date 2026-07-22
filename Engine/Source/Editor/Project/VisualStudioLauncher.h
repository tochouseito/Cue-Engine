#pragma once

/// **********************************************************************
/// Asset Browser で選択した Script source を Visual Studio で開く
/// **********************************************************************

// === Base includes ===
#include <CueResult.h>

namespace Cue::Core::IO
{
    class Path;
}

namespace Cue::Editor
{
    /// @brief GameScript CMake Project と選択済み Script source を Visual Studio で開く
    [[nodiscard]] Result open_script_asset_in_visual_studio(
        const Core::IO::Path& a_sourcePath,
        const Core::IO::Path& a_scriptProjectRoot) noexcept;

    /// @brief GameScript の CMake Project を Visual Studio で開く
    [[nodiscard]] Result open_script_project_in_visual_studio(
        const Core::IO::Path& a_projectRoot) noexcept;
} // namespace Cue::Editor
