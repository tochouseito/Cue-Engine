#include "ProjectSettings.h"

// === External includes ===
#include <nlohmann/json.hpp>

// === C++ includes ===
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

        [[nodiscard]] std::string make_project_relative_path(
            const Core::IO::Path& a_projectRoot,
            const Core::IO::Path& a_path)
        {
            // Project 内の asset path は相対化し、Project folder を移動しても cueproject.json を再利用できるようにする。
            const Core::IO::Path normalizedRoot = a_projectRoot.normalize();
            const Core::IO::Path normalizedPath = a_path.normalize();
            const std::string& rootText = normalizedRoot.utf8();
            const std::string& pathText = normalizedPath.utf8();
            if (!normalizedPath.is_absolute() || !starts_with_path_segment(pathText, rootText))
            {
                return pathText;
            }
            if (pathText.size() == rootText.size())
            {
                return {};
            }

            return pathText.substr(rootText.size() + 1u);
        }
    } // namespace

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
            settings.engineVersion = rootJson.value("engineVersion", 1u);
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

    Result save_project_settings(
        Core::IO::IFileSystem& a_fileSystem,
        const ProjectSettings& a_settings) noexcept
    {
        if (a_settings.root.is_empty())
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Project root is empty.");
        }

        try
        {
            const Core::IO::Path projectRoot = a_settings.root.normalize();
            Core::IO::Path assetRoot = a_settings.assetRoot;
            if (assetRoot.is_empty())
            {
                assetRoot = Core::IO::Path::join(projectRoot, Core::IO::Path("Assets"));
            }
            else if (!assetRoot.is_absolute())
            {
                assetRoot = Core::IO::Path::join(projectRoot, assetRoot);
            }

            const nlohmann::json rootJson{
                {"assetRoot", make_project_relative_path(projectRoot, assetRoot)},
                {"engineVersion", a_settings.engineVersion},
                {"name", a_settings.name.empty() ? projectRoot.filename() : a_settings.name},
                {"startupScene", a_settings.startupScene}};
            const std::string text = rootJson.dump(4);

            std::vector<std::byte> fileData(text.size());
            if (!text.empty())
            {
                std::memcpy(fileData.data(), text.data(), text.size());
            }

            const Core::IO::Path projectFilePath =
                Core::IO::Path::join(projectRoot, Core::IO::Path("cueproject.json"));
            return a_fileSystem.write_all(projectFilePath, fileData, true);
        }
        catch (...)
        {
            return Result::fail(
                Code::GetFailed,
                Severity::Error,
                "Project file could not be written.");
        }
    }
} // namespace Cue::Editor
