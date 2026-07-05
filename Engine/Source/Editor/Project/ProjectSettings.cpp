#include "ProjectSettings.h"

// === External includes ===
#include <nlohmann/json.hpp>

// === C++ includes ===
#include <string>
#include <utility>
#include <vector>

namespace Cue::Editor
{
    Result load_project_settings(
        Core::IO::IFileSystem& a_fileSystem,
        const Core::IO::Path& a_root,
        ProjectSettings& a_outSettings) noexcept
    {
        a_outSettings = {};

        const Core::IO::Path projectRoot = a_root.normalize();
        bool projectRootExists = false;
        Result result = a_fileSystem.exists(projectRoot, &projectRootExists);
        if (!result)
        {
            return result;
        }
        if (!projectRootExists)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Project root was not found.");
        }

        const Core::IO::Path projectFilePath =
            Core::IO::Path::join(projectRoot, Core::IO::Path("cueproject.json"));
        std::vector<std::byte> fileData{};
        result = a_fileSystem.read_all(projectFilePath, &fileData);
        if (!result)
        {
            return result;
        }

        try
        {
            const std::string text(
                reinterpret_cast<const char*>(fileData.data()),
                fileData.size());
            const nlohmann::json rootJson = nlohmann::json::parse(text);

            ProjectSettings settings{};
            settings.name = rootJson.value("name", projectRoot.filename());
            settings.startupScene = rootJson.value("startupScene", std::string{});
            settings.root = projectRoot;

            Core::IO::Path assetRoot(rootJson.value("assetRoot", std::string("Assets")));
            if (assetRoot.is_empty())
            {
                assetRoot = Core::IO::Path("Assets");
            }
            if (!assetRoot.is_absolute())
            {
                assetRoot = Core::IO::Path::join(projectRoot, assetRoot);
            }
            settings.assetRoot = assetRoot.normalize();

            bool assetRootExists = false;
            result = a_fileSystem.exists(settings.assetRoot, &assetRootExists);
            if (!result)
            {
                return result;
            }
            if (!assetRootExists)
            {
                return Result::fail(
                    Code::NotFound,
                    Severity::Error,
                    "Project asset root was not found.");
            }

            a_outSettings = std::move(settings);
            return Result::ok();
        }
        catch (...)
        {
            return Result::fail(
                Code::GetFailed,
                Severity::Error,
                "Project file could not be parsed.");
        }
    }
} // namespace Cue::Editor
