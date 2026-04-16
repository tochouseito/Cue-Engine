#pragma once

// === Base includes ===
#include <Result.h>

// === C++ includes ===
#include <string>

namespace Cue::Core::IO
{
    class IFileSystem;
    class Path;
}

namespace Cue::Editor
{
    struct ProjectGenerationRequest final
    {
        std::string projectName{};
        std::string baseDirectory{};
    };

    class ProjectGenerator final
    {
    public:
        explicit ProjectGenerator(Core::IO::IFileSystem& a_fileSystem) noexcept;
        ~ProjectGenerator() = default;

        [[nodiscard]] Result generate(const ProjectGenerationRequest& a_request,
            std::string& a_outProjectPath);

    private:
        [[nodiscard]] Result create_project_directories(
            const Core::IO::Path& a_projectPath);
        [[nodiscard]] Result write_script_project_files(
            const std::string& a_projectName,
            const Core::IO::Path& a_projectPath);
        [[nodiscard]] Result write_default_scene(
            const Core::IO::Path& a_projectPath);
        [[nodiscard]] Result write_project_file(
            const std::string& a_projectName,
            const Core::IO::Path& a_projectPath);
        [[nodiscard]] Result write_text_file(
            const Core::IO::Path& a_filePath,
            const std::string& a_text);
        [[nodiscard]] bool has_invalid_project_name_character(
            const std::string& a_projectName) const;
        [[nodiscard]] std::string make_cmake_project_name(
            const std::string& a_projectName) const;

    private:
        Core::IO::IFileSystem& m_fileSystem;
    };
}
