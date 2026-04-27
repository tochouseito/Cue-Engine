#pragma once

// === Base Includes ===
#include <Result.h>

// === Core Includes ===
#include <IO/IFileSystem.h>

namespace Cue::Editor
{
    class SoundCooker final
    {
    public:
        [[nodiscard]] static Result ensure_cuesound_is_up_to_date(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_sourcePath,
            const Core::IO::Path& a_destinationPath) noexcept;

    private:
        [[nodiscard]] static Result cook_wav_to_cuesound(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_sourcePath,
            const Core::IO::Path& a_destinationPath) noexcept;
    };
}
