// ModelImporter の役割と公開要素を定義する

#pragma once

// === Base includes ===
#include <Result.h>

// === Core includes ===
#include <IO/Path.h>
#include <Native/EngineNativeStruct.h>

namespace Cue::Editor
{
    class ModelImporter final
    {
    public:
        [[nodiscard]] static Result import_model(
            const Core::IO::Path& filePath,
            std::string_view modelName,
            Core::Native::ModelData& outModelData) noexcept;
    };
}
