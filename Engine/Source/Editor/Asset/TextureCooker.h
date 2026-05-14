#pragma once

// === Core includes ===
#include <IO/IFileSystem.h>

// === C++ includes ===
#include <array>

namespace Cue::Editor
{
    class TextureCooker final
    {
    public:
        [[nodiscard]] static Result ensure_dds_is_up_to_date(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_sourcePath,
            const Core::IO::Path& a_destinationPath) noexcept;
        [[nodiscard]] static Result make_cube_dds_from_faces(
            Core::IO::IFileSystem& a_fileSystem,
            const std::array<Core::IO::Path, 6>& a_facePaths,
            const Core::IO::Path& a_destinationPath) noexcept;

    private:
        [[nodiscard]] static Result cook_source_to_dds(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_sourcePath,
            const Core::IO::Path& a_destinationPath) noexcept;
    };
}
