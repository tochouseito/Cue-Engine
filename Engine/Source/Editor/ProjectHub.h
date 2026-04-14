#pragma once

// === C++ includes ===
#include <array>
#include <string>

namespace Cue::Core::IO
{
    class IFileSystem;
}

namespace Cue::Editor
{
    class ProjectHub final
    {
    public:
        explicit ProjectHub(Core::IO::IFileSystem& a_fileSystem);
        ~ProjectHub() = default;

        void update();

        [[nodiscard]] bool is_open() const;
        [[nodiscard]] std::string project_path() const;

    private:
        void open_create_project_modal();
        void draw_create_project_modal();
        bool browse_project_directory();
        bool open_existing_project();
        bool validate_project_directory(const std::string& a_projectPath);
        bool create_project();
        bool create_project_directories(const std::string& a_projectPath);
        bool write_default_scene(const std::string& a_projectPath);
        bool write_project_file(
            const std::string& a_projectName,
            const std::string& a_projectPath
        );
        [[nodiscard]] bool has_invalid_project_name_character(
            const std::string& a_projectName
        ) const;
        [[nodiscard]] std::string trim_text(const char* a_text) const;

    private:
        Core::IO::IFileSystem& m_fileSystem;
        bool m_isOpen = true;
        bool m_openCreateProjectModal = false;
        std::string m_projectPath = "";
        std::string m_errorMessage = "";
        std::array<char, 256> m_projectNameBuffer{};
        std::array<char, 1024> m_projectDirectoryBuffer{};
    };
}
