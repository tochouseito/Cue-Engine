#include "ProjectSelector.h"

// === Runtime includes ===
#include <Dialog/DialogService.h>
#include <IO/IFileSystem.h>

// === ImGui includes ===
#include <imgui.h>

namespace Cue::Editor
{
    namespace
    {
        constexpr const char* k_projectFileName = "cueproject.json";
    }

    ProjectSelector::ProjectSelector(
        PAL::IDialogService& a_dialogService,
        Core::IO::IFileSystem& a_fileSystem) noexcept
        : m_dialogService(&a_dialogService)
        , m_fileSystem(&a_fileSystem)
    {
    }

    void ProjectSelector::open_from_executable_directory()
    {
        m_isOpen = true;
        m_hasSelectedProject = false;
        m_selectedRoot = {};
        m_errorMessage.clear();

        if (m_fileSystem == nullptr)
        {
            m_errorMessage = "FileSystem が初期化されていません。";
            return;
        }

        Core::IO::Path executableDirectory{};
        const Result executableResult = m_fileSystem->executable_directory(executableDirectory);
        if (!executableResult)
        {
            m_errorMessage = "実行ファイルの場所を取得できませんでした。";
            return;
        }

        m_initialDirectory = executableDirectory.normalize();
    }

    void ProjectSelector::open() noexcept
    {
        m_isOpen = true;
        m_hasSelectedProject = false;
        m_selectedRoot = {};
        m_errorMessage.clear();
    }

    void ProjectSelector::update()
    {
        if (!m_isOpen)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(320.0f, 120.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("プロジェクト選択", nullptr, ImGuiWindowFlags_NoCollapse);

        if (ImGui::Button("プロジェクトを選択", ImVec2(-1.0f, 0.0f)))
        {
            open_project_folder_dialog();
        }

        if (!m_errorMessage.empty())
        {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 96, 96, 255));
            ImGui::TextWrapped("%s", m_errorMessage.c_str());
            ImGui::PopStyleColor();
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

    void ProjectSelector::open_project_folder_dialog()
    {
        if (m_dialogService == nullptr)
        {
            m_errorMessage = "DialogService が初期化されていません。";
            return;
        }

        PAL::FolderDialogDesc desc{};
        desc.title = "プロジェクトフォルダを選択";
        desc.initialDirectory = m_initialDirectory;

        Core::IO::Path selectedPath{};
        bool isSelected = false;
        const Result result = m_dialogService->open_folder_dialog(desc, selectedPath, isSelected);
        if (!result)
        {
            m_errorMessage = "フォルダ選択ダイアログを開けませんでした。";
            return;
        }
        if (!isSelected)
        {
            return;
        }

        if (!validate_project_directory(selectedPath))
        {
            return;
        }

        m_initialDirectory = selectedPath;
        m_selectedRoot = selectedPath.normalize();
        m_hasSelectedProject = true;
        m_isOpen = false;
        m_errorMessage.clear();
    }

    bool ProjectSelector::validate_project_directory(const Core::IO::Path& a_projectPath)
    {
        if (m_fileSystem == nullptr)
        {
            m_errorMessage = "FileSystem が初期化されていません。";
            return false;
        }
        if (a_projectPath.is_empty())
        {
            m_errorMessage = "プロジェクトフォルダを指定してください。";
            return false;
        }

        Core::IO::FileStat directoryStat{};
        Result result = m_fileSystem->stat(a_projectPath, &directoryStat);
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
            Core::IO::Path::join(a_projectPath, Core::IO::Path(k_projectFileName));
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
} // namespace Cue::Editor
