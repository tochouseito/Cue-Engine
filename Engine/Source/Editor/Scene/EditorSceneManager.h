#pragma once

/// ************************************************************************************
/// Editor が開いている Scene の状態を保持する
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

namespace Cue::GameCore
{
    class GameWorld;
}

namespace Cue::Editor
{
    /// @brief Editor の current scene 状態と GameWorld への読み込みを管理する
    class EditorSceneManager final
    {
    public:
        EditorSceneManager(Core::IO::IFileSystem& a_fileSystem, GameCore::GameWorld& a_world) noexcept;

        /// @brief Scene file を読み込み、GameWorld の内容を置き換える
        [[nodiscard]] Result open_scene(const Core::IO::Path& a_path) noexcept;

        /// @brief 現在の GameWorld を開いている Scene file へ保存する
        [[nodiscard]] Result save_scene() noexcept;

        /// @brief 開いている Scene 状態を破棄し、GameWorld を空にする
        void close_scene() noexcept;

        /// @brief 現在開いている Scene があるかを返す
        [[nodiscard]] bool has_scene() const noexcept
        {
            return m_hasScene;
        }

        /// @brief 現在開いている Scene の保存先を返す
        [[nodiscard]] const Core::IO::Path& current_scene_path() const noexcept
        {
            return m_currentScenePath;
        }

        /// @brief Editor 上で未保存変更があるかを返す
        [[nodiscard]] bool is_dirty() const noexcept;

    private:
        Core::IO::Path m_currentScenePath{};
        std::string m_sceneName{};
        Core::IO::IFileSystem* m_fileSystem = nullptr; // Scene file を読み書きする非所有 FileSystem
        GameCore::GameWorld* m_world = nullptr;        // Scene を展開する非所有 GameWorld
        std::uint64_t m_savedSceneRevision = 0;
        bool m_hasScene = false;
    };
} // namespace Cue::Editor
