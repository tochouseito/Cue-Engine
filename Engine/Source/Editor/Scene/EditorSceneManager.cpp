#include "EditorSceneManager.h"

// === Runtime includes ===
#include <GameCore/GameWorld.h>
#include <GameCore/SceneAsset.h>
#include <GameCore/SceneSerializer.h>
#include <IO/IFileSystem.h>
#include <CQRS/CQRS.h>

namespace Cue::Editor
{
    EditorSceneManager::EditorSceneManager(Core::IO::IFileSystem& a_fileSystem,
                                           GameCore::GameWorld& a_world,
                                           Core::CQRS::Bridge* a_commandBridge) noexcept
        : m_fileSystem(&a_fileSystem),
          m_world(&a_world),
          m_commandBridge(a_commandBridge)
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
            return close_scene();
        }

        const auto close_after_failure = [this](const Result& a_failure) -> Result
        {
            // 読み込み失敗後に中途半端な World を残すと、次回保存で破損した Scene を上書きするため必ず破棄します
            const Result closeResult = close_scene();
            return closeResult ? a_failure : closeResult;
        };

        bool exists = false;
        Result result = m_fileSystem->exists(a_path, &exists);
        if (!result)
        {
            return close_after_failure(result);
        }
        if (!exists)
        {
            return close_after_failure(Result::fail(
                Code::NotFound,
                Severity::Error,
                "Startup scene file was not found."));
        }

        GameCore::SceneAsset scene{};
        result = GameCore::load_scene_asset(*m_fileSystem, a_path, scene);
        if (!result)
        {
            return close_after_failure(result);
        }

        result = m_world->load_scene(scene);
        if (!result)
        {
            return close_after_failure(result);
        }

        m_currentScenePath = a_path;
        m_sceneName = scene.name.empty() ? a_path.stem() : scene.name;
        if (m_commandBridge != nullptr)
        {
            m_commandBridge->reset_history();
            m_savedHistoryCursor = m_commandBridge->history_cursor();
        }

        // 読み込み直後の World は保存済み状態として扱い、既存 Scene を開いただけで dirty 化しない。
        m_savedSceneRevision = m_world->scene_revision();
        m_hasScene = true;
        m_isUntitledScene = false;
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
        if (m_commandBridge != nullptr)
        {
            m_commandBridge->reset_history();
            m_savedHistoryCursor = m_commandBridge->history_cursor();
        }
        m_savedSceneRevision = m_world->scene_revision();
        m_hasScene = true;
        m_isUntitledScene = true;

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

        // 書き込み中に後続 command が反映された場合も dirty 状態を保てるよう、保存した snapshot の revision を記録する。
        const std::uint64_t sceneRevision = m_world->scene_revision();
        result = GameCore::save_scene_asset(*m_fileSystem, a_path, scene);
        if (!result)
        {
            return result;
        }

        m_currentScenePath = a_path;
        m_sceneName = a_name;
        m_savedSceneRevision = sceneRevision;
        m_savedHistoryCursor =
            m_commandBridge != nullptr ? m_commandBridge->history_cursor() : 0;
        m_isUntitledScene = false;
        return Result::ok();
    }

    Result EditorSceneManager::close_scene() noexcept
    {
        if (m_world != nullptr)
        {
            const Result result = m_world->clear();
            if (!result)
            {
                return result;
            }
        }

        m_currentScenePath = {};
        m_sceneName.clear();
        if (m_commandBridge != nullptr)
        {
            m_commandBridge->reset_history();
            m_savedHistoryCursor = m_commandBridge->history_cursor();
        }
        m_savedSceneRevision = m_world != nullptr ? m_world->scene_revision() : 0;
        m_hasScene = false;
        m_isUntitledScene = false;
        return Result::ok();
    }

    bool EditorSceneManager::is_dirty() const noexcept
    {
        if (!m_hasScene || m_world == nullptr)
        {
            return false;
        }

        if (m_isUntitledScene)
        {
            return true;
        }

        if (m_commandBridge != nullptr)
        {
            return m_commandBridge->history_cursor() != m_savedHistoryCursor;
        }

        return m_world->scene_revision() != m_savedSceneRevision;
    }
} // namespace Cue::Editor
