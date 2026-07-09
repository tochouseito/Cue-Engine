#pragma once

/// ************************************************************************************
/// Editor で開いている Project の設定と状態を保持する
/// ************************************************************************************

// === Base includes ===
#include <CueResult.h>

// === Runtime includes ===
#include <IO/Path.h>

// === Editor includes ===
#include "Scene/SceneAsset.h"

// === C++ includes ===
#include <string>

namespace Cue::Core::IO
{
    class IFileSystem;
}

namespace Cue::Editor
{
    /// @brief cueproject.json から読み込んだ Editor Project の状態を保持する
    class EditorProject final
    {
    public:
        explicit EditorProject(Core::IO::IFileSystem& a_fileSystem) noexcept;

        /// @brief Project 設定を読み込み、Editor 側の Project 状態を更新する
        [[nodiscard]] Result load(const Core::IO::Path& a_root) noexcept;

        /// @brief Project root path を返す
        [[nodiscard]] const Core::IO::Path& root_path() const noexcept
        {
            return m_rootPath;
        }

        /// @brief Runtime の asset 解決に使う root path を返す
        [[nodiscard]] const Core::IO::Path& asset_root_path() const noexcept
        {
            return m_assetRootPath;
        }

        /// @brief Project 表示名を返す
        [[nodiscard]] const std::string& name() const noexcept
        {
            return m_name;
        }

        /// @brief 起動時に読み込む Scene 名を返す
        [[nodiscard]] const std::string& startup_scene() const noexcept
        {
            return m_startupScene;
        }

        /// @brief 起動時に読み込む Scene の解決済み path を返す
        [[nodiscard]] const Core::IO::Path& startup_scene_path() const noexcept
        {
            return m_startupScenePath;
        }

        /// @brief Project で現在開いている Scene があるかを返す
        [[nodiscard]] bool has_active_scene() const noexcept
        {
            return m_hasActiveScene;
        }

        /// @brief 現在開いている Scene asset を返す
        [[nodiscard]] const SceneAsset& active_scene() const noexcept
        {
            return m_activeScene;
        }

        /// @brief 現在開いている Scene asset の保存先を返す
        [[nodiscard]] const Core::IO::Path& active_scene_path() const noexcept
        {
            return m_activeScenePath;
        }

    private:
        Core::IO::Path m_rootPath{};
        Core::IO::Path m_assetRootPath{};
        Core::IO::Path m_startupScenePath{};
        Core::IO::Path m_activeScenePath{};
        SceneAsset m_activeScene{};
        std::string m_name{};
        std::string m_startupScene{};
        Core::IO::IFileSystem* m_fileSystem = nullptr; // Project 設定を読み込む非所有 FileSystem
        bool m_hasActiveScene = false;
    };
} // namespace Cue::Editor
