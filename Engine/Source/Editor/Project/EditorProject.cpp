#include "EditorProject.h"

// === Editor includes ===
#include "ProjectSettings.h"

namespace Cue::Editor
{
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
        return Result::ok();
    }
} // namespace Cue::Editor
