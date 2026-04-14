#pragma once

// === Base includes ===
#include <Result.h>

// === Core includes ===
#include <IO/IFileSystem.h>

// === Engine includes ===
#include "SceneAsset.h"

namespace Cue::GameCore
{
    class SceneSerializer final
    {
    public:
        static constexpr uint32_t k_currentVersion = 1;

        /// @brief SceneAsset を `.cuescene` JSON へ保存します。
        [[nodiscard]] static Result save_scene_asset(const SceneAsset& a_sceneAsset,
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_filePath) noexcept;

        /// @brief `.cuescene` JSON から SceneAsset を復元します。
        [[nodiscard]] static Result load_scene_asset(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_filePath,
            SceneAsset& a_outSceneAsset) noexcept;
    };
}
