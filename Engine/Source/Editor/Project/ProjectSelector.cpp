#include "ProjectSelector.h"

// === Runtime includes ===
#include <IO/IFileSystem.h>

// === ImGui includes ===
#include <imgui.h>

// === C++ includes ===
#include <algorithm>
#include <cstring>

namespace Cue::Editor
{
    namespace
    {
        constexpr const char* k_projectFileName = "cueproject.json";
    }

    ProjectSelector::ProjectSelector(Core::IO::IFileSystem& a_fileSystem) noexcept
        : m_fileSystem(&a_fileSystem)
    {}

    void ProjectSelector::open_from_executable_directory()
    {
        m_isOpen = true;
        m_hasSelectedProject = false;
        m_selectedRoot = {};
        m_errorMessage.clear();

        Core::IO::Path executableDirectory{};
        const Result executableResult = m_fileSystem->executable_directory(executableDirectory);
        if (!executableResult)
        {
            m_errorMessage = "実行ファイルの場所を取得できませんでした。";
            return;
        }

        Core::IO::Path current = executableDirectory.normalize();
        while (!current.is_empty())
        {
            std::vector<ProjectCandidate> candidates{};
            if (collect_candidates(current, candidates) && !candidates.empty())
            {
                m_searchRoot = current;
                m_candidates = std::move(candidates);
                set_search_root_text(m_searchRoot.utf8());
                set_project_path_text(m_candidates.front().root.utf8());
                return;
            }

            const Core::IO::Path parent = current.parent();
            if (parent.utf8() == current.utf8())
            {
                break;
            }
            current = parent;
        }

        set_search_root(executableDirectory);
        m_errorMessage = "cueproject.json を持つ Project が見つかりませんでした。";
    }

    void ProjectSelector::open() noexcept
    {
        m_isOpen = true;
        m_hasSelectedProject = false;
        m_selectedRoot = {};
    }

    void ProjectSelector::update()
    {
        if (!m_isOpen)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(560.0f, 420.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("プロジェクト選択", nullptr, ImGuiWindowFlags_NoCollapse);

        ImGui::TextUnformatted("検索フォルダ");
        ImGui::SetNextItemWidth(-96.0f);
        ImGui::InputText("##ProjectSearchRoot", m_searchRootBuffer.data(), m_searchRootBuffer.size());
        ImGui::SameLine();
        if (ImGui::Button("再検索"))
        {
            refresh_candidates_from_buffer();
        }

        ImGui::Spacing();
        ImGui::BeginChild("ProjectCandidateList", ImVec2(0.0f, 180.0f), true);
        for (const ProjectCandidate& candidate : m_candidates)
        {
            const bool isSelected = trim_text(m_projectPathBuffer.data()) == candidate.root.utf8();
            if (ImGui::Selectable(candidate.name.c_str(), isSelected))
            {
                set_project_path_text(candidate.root.utf8());
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", candidate.root.utf8().c_str());
            }
        }
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::TextUnformatted("プロジェクトフォルダ");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##ProjectPath", m_projectPathBuffer.data(), m_projectPathBuffer.size());

        if (!m_errorMessage.empty())
        {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 96, 96, 255));
            ImGui::TextWrapped("%s", m_errorMessage.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        if (ImGui::Button("読み込み"))
        {
            const std::string projectPath = trim_text(m_projectPathBuffer.data());
            if (validate_project_directory(projectPath))
            {
                m_selectedRoot = Core::IO::Path(projectPath).normalize();
                m_hasSelectedProject = true;
                m_isOpen = false;
                m_errorMessage.clear();
            }
        }

        ImGui::End();
    }

    bool ProjectSelector::consume_selected_project(Core::IO::Path& a_outRoot) noexcept
    {
        if (!m_hasSelectedProject)
        {
            return false;
        }

        a_outRoot = m_selectedRoot;
        m_selectedRoot = {};
        m_hasSelectedProject = false;
        return true;
    }

    void ProjectSelector::show_error(std::string_view a_message)
    {
        m_errorMessage.assign(a_message.data(), a_message.size());
        m_isOpen = true;
    }

    void ProjectSelector::refresh_candidates_from_buffer()
    {
        set_search_root(Core::IO::Path(trim_text(m_searchRootBuffer.data())));
    }

    void ProjectSelector::set_search_root(const Core::IO::Path& a_root)
    {
        m_searchRoot = a_root.normalize();
        set_search_root_text(m_searchRoot.utf8());

        std::vector<ProjectCandidate> candidates{};
        if (!collect_candidates(m_searchRoot, candidates))
        {
            return;
        }

        m_candidates = std::move(candidates);
        m_errorMessage.clear();
        if (!m_candidates.empty())
        {
            set_project_path_text(m_candidates.front().root.utf8());
        }
    }

    bool ProjectSelector::collect_candidates(const Core::IO::Path& a_root, std::vector<ProjectCandidate>& a_outCandidates)
    {
        a_outCandidates.clear();
        if (m_fileSystem == nullptr)
        {
            m_errorMessage = "FileSystem が初期化されていません。";
            return false;
        }

        Core::IO::FileStat rootStat{};
        Result result = m_fileSystem->stat(a_root, &rootStat);
        if (!result || rootStat.type != Core::IO::FileType::directory)
        {
            m_errorMessage = "検索フォルダを開けませんでした。";
            return false;
        }

        std::vector<Core::IO::Path> entries{};
        result = m_fileSystem->list_directory(a_root, &entries);
        if (!result)
        {
            m_errorMessage = "検索フォルダを列挙できませんでした。";
            return false;
        }

        for (const Core::IO::Path& entry : entries)
        {
            Core::IO::FileStat entryStat{};
            result = m_fileSystem->stat(entry, &entryStat);
            if (!result || entryStat.type != Core::IO::FileType::directory)
            {
                continue;
            }

            const Core::IO::Path projectFilePath =
                Core::IO::Path::join(entry, Core::IO::Path(k_projectFileName));
            bool projectFileExists = false;
            result = m_fileSystem->exists(projectFilePath, &projectFileExists);
            if (!result || !projectFileExists)
            {
                continue;
            }

            Core::IO::FileStat projectFileStat{};
            result = m_fileSystem->stat(projectFilePath, &projectFileStat);
            if (!result || projectFileStat.type != Core::IO::FileType::regular)
            {
                continue;
            }

            a_outCandidates.push_back(ProjectCandidate{ entry.filename(), entry.normalize() });
        }

        std::sort(
            a_outCandidates.begin(),
            a_outCandidates.end(),
            [](const ProjectCandidate& a_left, const ProjectCandidate& a_right)
            {
                return a_left.name < a_right.name;
            });
        return true;
    }

    bool ProjectSelector::validate_project_directory(const std::string& a_projectPath)
    {
        if (a_projectPath.empty())
        {
            m_errorMessage = "プロジェクトフォルダを指定してください。";
            return false;
        }

        const Core::IO::Path projectPath(a_projectPath);
        Core::IO::FileStat directoryStat{};
        Result result = m_fileSystem->stat(projectPath, &directoryStat);
        if (!result)
        {
            m_errorMessage = "プロジェクトフォルダの情報取得に失敗しました。";
            return false;
        }
        if (directoryStat.type != Core::IO::FileType::directory)
        {
            m_errorMessage = "指定したパスはフォルダではありません。";
            return false;
        }

        const Core::IO::Path projectFilePath =
            Core::IO::Path::join(projectPath, Core::IO::Path(k_projectFileName));
        Core::IO::FileStat projectFileStat{};
        result = m_fileSystem->stat(projectFilePath, &projectFileStat);
        if (!result)
        {
            m_errorMessage = "cueproject.json が見つかりません。";
            return false;
        }
        if (projectFileStat.type != Core::IO::FileType::regular)
        {
            m_errorMessage = "cueproject.json がファイルではありません。";
            return false;
        }

        return true;
    }

    void ProjectSelector::set_project_path_text(std::string_view a_text)
    {
        m_projectPathBuffer.fill('\0');
        const size_t count = (std::min)(a_text.size(), m_projectPathBuffer.size() - 1);
        std::memcpy(m_projectPathBuffer.data(), a_text.data(), count);
    }

    void ProjectSelector::set_search_root_text(std::string_view a_text)
    {
        m_searchRootBuffer.fill('\0');
        const size_t count = (std::min)(a_text.size(), m_searchRootBuffer.size() - 1);
        std::memcpy(m_searchRootBuffer.data(), a_text.data(), count);
    }

    std::string ProjectSelector::trim_text(const char* a_text) const
    {
        if (a_text == nullptr)
        {
            return {};
        }

        std::string text = a_text;
        const size_t first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
        {
            return {};
        }

        const size_t last = text.find_last_not_of(" \t\r\n");
        return text.substr(first, (last - first) + 1);
    }
} // namespace Cue::Editor
