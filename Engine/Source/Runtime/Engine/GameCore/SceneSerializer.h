// SceneSerializer の役割と公開要素を定義する

#pragma once

// === Base includes ===
#include <Result.h>

// === Core includes ===
#include <IO/IFileSystem.h>

// === Engine includes ===
#include "SceneAsset.h"

// === C++ includes ===
#include <string_view>

namespace Cue
{
    class AssetManager;
}

namespace Cue::GameCore
{
    class SceneSerializer final
    {
    public:
        using ScriptFieldSerializePredicate = bool (*)(
            std::string_view a_scriptClassName,
            std::string_view a_fieldName,
            void* a_userData);

        struct SaveOptions final
        {
            ScriptFieldSerializePredicate shouldSerializeScriptField = nullptr;
            void* userData = nullptr;
            const AssetManager* assetManager = nullptr;
        };

        struct LoadOptions final
        {
            const AssetManager* assetManager = nullptr;
        };

        static constexpr uint32_t k_currentVersion = 2;

        /// @brief SceneAsset を `.cuescene` JSON へ保存し
        [[nodiscard]] static Result save_scene_asset(const SceneAsset& a_sceneAsset,
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_filePath,
            const SaveOptions& a_options = {}) noexcept;

        /// @brief `.cuescene` JSON から SceneAsset を復元し
        [[nodiscard]] static Result load_scene_asset(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_filePath,
            SceneAsset& a_outSceneAsset,
            const LoadOptions& a_options = {}) noexcept;
    };
}
