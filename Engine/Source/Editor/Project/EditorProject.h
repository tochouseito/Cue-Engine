#pragma once

/// ************************************************************************************
/// Editor で開いている Project の設定と状態を保持する
/// ************************************************************************************

// === Base includes ===
#include <CueResult.h>

// === Runtime includes ===
#include <IO/Path.h>

// === C++ includes ===
#include <cstdint>
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

        /// @brief 指定 Scene を次回起動時の Startup Scene に設定する
        [[nodiscard]] Result set_startup_scene_path(const Core::IO::Path& a_path) noexcept;

    private:
        Core::IO::Path m_rootPath{};
        Core::IO::Path m_assetRootPath{};
        Core::IO::Path m_startupScenePath{};
        std::string m_name{};
        std::string m_startupScene{};
        std::uint32_t m_engineVersion = 1;
        Core::IO::IFileSystem* m_fileSystem = nullptr; // Project 設定を読み込む非所有 FileSystem
    };
} // namespace Cue::Editor
