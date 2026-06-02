// ModelCooker の役割と公開要素を定義する

#pragma once

// === Base Includes ===
#include <Result.h>

// === Core Includes ===
#include <IO/IFileSystem.h>

namespace Cue::Editor
{
    class ModelCooker final
    {
    public:
        [[nodiscard]] static Result ensure_cuemodel_is_up_to_date(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_sourcePath,
            const Core::IO::Path& a_destinationPath) noexcept;

    private:
        [[nodiscard]] static Result cook_model_to_cuemodel(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_sourcePath,
            const Core::IO::Path& a_destinationPath) noexcept;
    };
}
