#pragma once

/// ************************************************************************************
/// cueproject.json から Editor が扱う Project 設定を読み込む
/// ************************************************************************************

// === Base includes ===
#include <CueResult.h>

// === Core includes ===
#include <IO/IFileSystem.h>
#include <IO/Path.h>

// === C++ includes ===
#include <string>

namespace Cue::Editor
{
    struct ProjectSettings final
    {
        std::string name{};
        std::string startupScene{};
        Core::IO::Path root{};
        Core::IO::Path assetRoot{};
    };

    /// @brief cueproject.json から Project の最小設定を読み込む。
    [[nodiscard]] Result load_project_settings(
        Core::IO::IFileSystem& a_fileSystem,
        const Core::IO::Path& a_root,
        ProjectSettings& a_outSettings) noexcept;
} // namespace Cue::Editor
