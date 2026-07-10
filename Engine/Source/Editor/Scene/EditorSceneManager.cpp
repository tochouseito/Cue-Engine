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
        m_sceneName = scene.name.empty() ? a_path.stem() : scene.name;
        m_savedSceneRevision = m_world->scene_revision();
        m_hasScene = true;
        return Result::ok();
    }

    Result EditorSceneManager::save_scene() noexcept
    {
        if (m_fileSystem == nullptr || m_world == nullptr || !m_hasScene || m_currentScenePath.is_empty())
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Warning,
                "EditorSceneManager does not have a scene to save.");
        }

        return save_scene_to(m_currentScenePath, m_sceneName);
    }

    Result EditorSceneManager::new_scene() noexcept
    {
        if (m_world == nullptr)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "EditorSceneManager world is not initialized.");
        }

        Result result = m_world->clear();
        if (!result)
        {
            return result;
        }

        m_currentScenePath = {};
        m_sceneName = "Untitled";
        m_savedSceneRevision = m_world->scene_revision();
        m_hasScene = true;

        // 保存先を持たない新規 Scene も未保存として扱い、保存操作へ導く。
        m_world->record_scene_edit();
        return Result::ok();
    }

    Result EditorSceneManager::save_scene_as(const Core::IO::Path& a_path) noexcept
    {
        if (a_path.is_empty())
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Warning,
                "Scene save path is empty.");
        }

        return save_scene_to(a_path.normalize(), a_path.stem());
    }

    Result EditorSceneManager::save_scene_to(const Core::IO::Path& a_path, const std::string& a_name) noexcept
    {
        if (m_fileSystem == nullptr || m_world == nullptr || !m_hasScene)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Warning,
                "EditorSceneManager does not have a scene to save.");
        }

        GameCore::SceneAsset scene{};
        Result result = m_world->make_scene_asset(a_name, scene);
        if (!result)
        {
            return result;
        }

        const std::uint64_t sceneRevision = m_world->scene_revision();
        result = GameCore::save_scene_asset(*m_fileSystem, a_path, scene);
        if (!result)
        {
            return result;
        }

        m_currentScenePath = a_path;
        m_sceneName = a_name;
        m_savedSceneRevision = sceneRevision;
        return Result::ok();
    }

    void EditorSceneManager::close_scene() noexcept
    {
        if (m_world != nullptr)
        {
            (void)m_world->clear();
        }

        m_currentScenePath = {};
        m_sceneName.clear();
        m_savedSceneRevision = m_world != nullptr ? m_world->scene_revision() : 0;
        m_hasScene = false;
    }

    bool EditorSceneManager::is_dirty() const noexcept
    {
        return m_hasScene && m_world != nullptr && m_world->scene_revision() != m_savedSceneRevision;
    }
} // namespace Cue::Editor
