// SoundCooker の役割と公開要素を定義する

#pragma once

// === Base Includes ===
#include <Result.h>

// === Core Includes ===
#include <IO/IFileSystem.h>

// === C++ Includes ===
#include <cstdint>

namespace Cue::Editor
{
    enum class SoundCookFormat : uint8_t
    {
        Pcm,
        Adpcm
    };

    class SoundCooker final
    {
    public:
        [[nodiscard]] static Result ensure_cuesound_is_up_to_date(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_sourcePath,
            const Core::IO::Path& a_destinationPath,
            SoundCookFormat a_format = SoundCookFormat::Pcm) noexcept;

    private:
        [[nodiscard]] static Result cook_wav_to_cuesound(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_sourcePath,
            const Core::IO::Path& a_destinationPath,
            SoundCookFormat a_format) noexcept;
    };
}
