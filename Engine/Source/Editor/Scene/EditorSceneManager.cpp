#include "EditorSceneManager.h"

// === Runtime includes ===
#include <GameCore/GameWorld.h>
#include <GameCore/SceneAsset.h>
#include <GameCore/SceneSerializer.h>
#include <IO/IFileSystem.h>

namespace Cue::Editor
{
    EditorSceneManager::EditorSceneManager(Core::IO::IFileSystem& a_fileSystem, GameCore::GameWorld& a_world) noexcept
        : m_fileSystem(&a_fileSystem),
          m_world(&a_world)
    {
    }

    Result EditorSceneManager::open_scene(const Core::IO::Path& a_path) noexcept
    {
        if (m_fileSystem == nullptr || m_world == nullptr)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "EditorSceneManager dependencies are not initialized.");
        }

        if (a_path.is_empty())
        {
            close_scene();
            return Result::ok();
        }

        bool exists = false;
        Result result = m_fileSystem->exists(a_path, &exists);
        if (!result)
        {
            close_scene();
            return result;
        }
        if (!exists)
        {
            close_scene();
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Startup scene file was not found.");
        }

        GameCore::SceneAsset scene{};
        result = GameCore::load_scene_asset(*m_fileSystem, a_path, scene);
        if (!result)
        {
            close_scene();
            return result;
        }

        result = m_world->load_scene(scene);
        if (!result)
        {
            close_scene();
            return result;
        }

        m_currentScenePath = a_path;
        m_hasScene = true;
        m_isDirty = false;
        return Result::ok();
    }

    void EditorSceneManager::close_scene() noexcept
    {
        if (m_world != nullptr)
        {
            (void)m_world->clear();
        }

        m_currentScenePath = {};
        m_hasScene = false;
        m_isDirty = false;
    }
} // namespace Cue::Editor
