#pragma once

// === Core includes ===
#include <IO/IFileSystem.h>

namespace Cue::Editor
{
    class TextureCooker final
    {
    public:
        [[nodiscard]] static Result ensure_cuetexture_is_up_to_date(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_sourcePath,
            const Core::IO::Path& a_destinationPath) noexcept;

    private:
        [[nodiscard]] static Result cook_source_to_cuetexture(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_sourcePath,
            const Core::IO::Path& a_destinationPath) noexcept;
    };
}
