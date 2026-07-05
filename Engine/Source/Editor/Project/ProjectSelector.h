#pragma once

/// ************************************************************************************
/// cueproject.json を持つ Project を選択する Editor UI
/// ************************************************************************************

// === Runtime includes ===
#include <IO/Path.h>

// === C++ includes ===
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace Cue::Core::IO
{
    class IFileSystem;
}

namespace Cue::Editor
{
    struct ProjectCandidate final
    {
        std::string name{};
        Core::IO::Path root{};
    };

    /// @brief Project 読み込み前に cueproject.json を持つフォルダを選択する。
    class ProjectSelector final
    {
    public:
        explicit ProjectSelector(Core::IO::IFileSystem& a_fileSystem) noexcept;
        ~ProjectSelector() = default;

        /// @brief 実行ファイルの配置場所から上位へ探索し、Project 選択 UI を開く。
        void open_from_executable_directory();

        /// @brief 現在の探索結果を維持したまま Project 選択 UI を開く。
        void open() noexcept;

        /// @brief Project 選択 UI を描画する。
        void update();

        /// @brief 読み込み要求された Project root を取り出す。
        bool consume_selected_project(Core::IO::Path& a_outRoot) noexcept;

        /// @brief Project 読み込みで発生したエラーを UI に表示する。
        void show_error(std::string_view a_message);

        [[nodiscard]] bool is_open() const noexcept
        {
            return m_isOpen;
        }

    private:
        void refresh_candidates_from_buffer();
        void set_search_root(const Core::IO::Path& a_root);
        bool collect_candidates(const Core::IO::Path& a_root, std::vector<ProjectCandidate>& a_outCandidates);
        bool validate_project_directory(const std::string& a_projectPath);
        void set_project_path_text(std::string_view a_text);
        void set_search_root_text(std::string_view a_text);
        [[nodiscard]] std::string trim_text(const char* a_text) const;

        std::vector<ProjectCandidate> m_candidates{};
        std::array<char, 1024> m_searchRootBuffer{};
        std::array<char, 1024> m_projectPathBuffer{};
        Core::IO::Path m_searchRoot{};
        Core::IO::Path m_selectedRoot{};
        std::string m_errorMessage{};
        Core::IO::IFileSystem* m_fileSystem = nullptr; // Project 候補の検出に使う非所有 FileSystem
        bool m_isOpen = false;
        bool m_hasSelectedProject = false;
    };
} // namespace Cue::Editor
