#pragma once

/// ************************************************************************************
/// Scene asset と JSON ファイルの相互変換
/// ************************************************************************************

// === Base includes ===
#include <CueResult.h>

// === Core includes ===
#include <IO/Path.h>

// === Editor includes ===
#include "SceneAsset.h"

namespace Cue::Core::IO
{
    class IFileSystem;
}

namespace Cue::Editor
{
    /// @brief Scene asset を JSON ファイルから読み込む。
    [[nodiscard]] Result load_scene_asset(
        Core::IO::IFileSystem& a_fileSystem,
        const Core::IO::Path& a_path,
        SceneAsset& a_outScene) noexcept;

    /// @brief Scene asset を JSON ファイルへ保存する。
    [[nodiscard]] Result save_scene_asset(
        Core::IO::IFileSystem& a_fileSystem,
        const Core::IO::Path& a_path,
        const SceneAsset& a_scene) noexcept;
} // namespace Cue::Editor
