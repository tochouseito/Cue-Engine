#include "EditorProject.h"

// === Editor includes ===
#include "ProjectSettings.h"

// === C++ includes ===
#include <string_view>

namespace Cue::Editor
{
    namespace
    {
        [[nodiscard]] bool starts_with_path_segment(std::string_view a_path, std::string_view a_segment) noexcept
        {
            if (a_segment.empty() || a_path.size() < a_segment.size())
            {
                return false;
            }

            if (a_path.substr(0, a_segment.size()) != a_segment)
            {
                return false;
            }

            return a_path.size() == a_segment.size() || a_path[a_segment.size()] == '/';
        }

        [[nodiscard]] Core::IO::Path resolve_startup_scene_path(
            const Core::IO::Path& a_rootPath,
            const Core::IO::Path& a_assetRootPath,
            std::string_view a_startupScene) noexcept
        {
            if (a_startupScene.empty())
            {
                return {};
            }

            const Core::IO::Path scenePath{std::string(a_startupScene)};
            if (scenePath.is_absolute())
            {
                return scenePath.normalize();
            }

            const Core::IO::Path normalizedScenePath = scenePath.normalize();
            const std::string assetRootName = a_assetRootPath.filename();
            const std::string& scenePathText = normalizedScenePath.utf8();

            // 既存 project は "Assets/Scenes/..." を持つため、asset root 名から始まる path は project root 相対として扱う
            if (starts_with_path_segment(scenePathText, assetRootName))
            {
                return Core::IO::Path::join(a_rootPath, normalizedScenePath);
            }

            return Core::IO::Path::join(a_assetRootPath, normalizedScenePath);
        }
    } // namespace

    EditorProject::EditorProject(Core::IO::IFileSystem& a_fileSystem) noexcept
        : m_fileSystem(&a_fileSystem)
    {
    }

    Result EditorProject::load(const Core::IO::Path& a_root) noexcept
    {
        if (m_fileSystem == nullptr)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "EditorProject file system is not initialized.");
        }

        ProjectSettings settings{};
        Result result = load_project_settings(*m_fileSystem, a_root, settings);
        if (!result)
        {
            return result;
        }

        m_rootPath = settings.root;
        m_assetRootPath = settings.assetRoot;
        m_name = settings.name;
        m_startupScene = settings.startupScene;
        m_startupScenePath = resolve_startup_scene_path(m_rootPath, m_assetRootPath, m_startupScene);

        // Scene の読み込みに失敗した状態が Project 切り替え後へ残らないよう、Project load で所有状態を初期化する
        m_activeScenePath = {};
        m_activeScene = {};
        m_hasActiveScene = false;
        return Result::ok();
    }
} // namespace Cue::Editor
