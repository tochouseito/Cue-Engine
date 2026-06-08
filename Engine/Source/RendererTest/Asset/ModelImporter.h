#pragma once

/// ****************************************************************************
/// モデルインポーター
/// ****************************************************************************

// === Base includes ===
#include <CueResult.h>

// === Core includes ===
#include <IO/Path.h>
#include <Native/EngineNativeStruct.h>

// === C++ includes ===
#include <array>
#include <string_view>

namespace Cue::Editor
{
class ModelImporter final
{
  public:
    struct LodGroupSettings final
    {
        std::string_view name = "Default";
        std::array<float, 3> indexRatios{0.5f, 0.15f, 0.01f};
        bool generateBillboardLod = true;
        float occluderIndexRatio = 0.0f;
    };

    [[nodiscard]] static Result import_model(
        const Core::IO::Path &filePath, std::string_view modelName,
        Core::Native::ModelData &outModelData) noexcept;

    [[nodiscard]] static Result import_model(
        const Core::IO::Path &filePath, std::string_view modelName,
        const LodGroupSettings &lodGroupSettings,
        Core::Native::ModelData &outModelData) noexcept;
};
} // namespace Cue::Editor
