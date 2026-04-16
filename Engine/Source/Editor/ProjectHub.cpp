#include "ProjectHub.h"

// === Base includes ===
#include <Result.h>

// === Core includes ===
#include <IO/IFileSystem.h>
#include <IO/Path.h>

// === PAL includes ===
#include <Dialog/DirectoryDialog.h>

// === C++ includes ===
#include <algorithm>

// === ImGui includes ===
#include <imgui.h>

namespace Cue::Editor
{
    namespace
    {
        constexpr const char* k_createProjectPopupName = "新規プロジェクト作成";
        constexpr const char* k_defaultProjectRoot = CUE_PROJECT_ROOT_PATH;
    }

    ProjectHub::ProjectHub(Core::IO::IFileSystem& a_fileSystem)
        : m_fileSystem(a_fileSystem)
        , m_projectGenerator(a_fileSystem)
    {
        const std::string defaultDirectory = k_defaultProjectRoot;
        defaultDirectory.copy(
            m_projectDirectoryBuffer.data(),
            m_projectDirectoryBuffer.size() - 1
        );
        m_projectDirectoryBuffer.back() = '\0';
    }

    void ProjectHub::update()
    {
        ImGui::Begin("Project Hub");

        if (ImGui::Button("新規プロジェクト作成"))
        {
            open_create_project_modal();
        }

        if (ImGui::Button("プロジェクトを開く"))
        {
            open_existing_project();
        }

        if (!m_errorMessage.empty())
        {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 96, 96, 255));
            ImGui::TextWrapped("%s", m_errorMessage.c_str());
            ImGui::PopStyleColor();
        }

        draw_create_project_modal();

        ImGui::End();
    }

    bool ProjectHub::is_open() const
    {
        return m_isOpen;
    }

    std::string ProjectHub::project_path() const
    {
        return m_projectPath;
    }

    void ProjectHub::open_create_project_modal()
    {
        m_errorMessage.clear();
        m_projectNameBuffer.fill('\0');
        m_openCreateProjectModal = true;
        ImGui::OpenPopup(k_createProjectPopupName);
    }

    void ProjectHub::draw_create_project_modal()
    {
        if (m_openCreateProjectModal)
        {
            ImGui::OpenPopup(k_createProjectPopupName);
            m_openCreateProjectModal = false;
        }

        if (!ImGui::BeginPopupModal(
            k_createProjectPopupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        ImGui::Text("プロジェクト名");
        ImGui::SetNextItemWidth(320.0f);
        ImGui::InputText("##ProjectName", m_projectNameBuffer.data(),
            m_projectNameBuffer.size());

        ImGui::Spacing();
        ImGui::Text("作成先ディレクトリ");
        ImGui::SetNextItemWidth(320.0f);
        ImGui::InputText("##ProjectDirectory", m_projectDirectoryBuffer.data(),
            m_projectDirectoryBuffer.size());
        ImGui::SameLine();
        if (ImGui::Button("参照..."))
        {
            browse_project_directory();
        }

        if (!m_errorMessage.empty())
        {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 96, 96, 255));
            ImGui::TextWrapped("%s", m_errorMessage.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();

        const bool canCreate =
            !trim_text(m_projectNameBuffer.data()).empty() &&
            !trim_text(m_projectDirectoryBuffer.data()).empty();

        if (!canCreate)
        {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("作成") && canCreate)
        {
            if (create_project())
            {
                ImGui::CloseCurrentPopup();
            }
        }

        if (!canCreate)
        {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (ImGui::Button("キャンセル"))
        {
            m_errorMessage.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    bool ProjectHub::browse_project_directory()
    {
        bool wasSelected = false;
        std::string selectedDirectory{};
        const Result result = Cue::PAL::Win::pick_directory_dialog(
            "プロジェクト作成先を選択",
            trim_text(m_projectDirectoryBuffer.data()),
            &selectedDirectory,
            &wasSelected
        );
        if (!result)
        {
            m_errorMessage = "フォルダ選択ダイアログを開けませんでした。";
            return false;
        }

        if (!wasSelected)
        {
            return false;
        }

        selectedDirectory.copy(
            m_projectDirectoryBuffer.data(),
            m_projectDirectoryBuffer.size() - 1
        );
        m_projectDirectoryBuffer[
            (std::min)(selectedDirectory.size(), m_projectDirectoryBuffer.size() - 1)] = '\0';
        m_errorMessage.clear();
        return true;
    }

    bool ProjectHub::open_existing_project()
    {
        bool wasSelected = false;
        std::string selectedDirectory{};
        const Result result = Cue::PAL::Win::pick_directory_dialog(
            "プロジェクトフォルダを選択",
            trim_text(m_projectDirectoryBuffer.data()),
            &selectedDirectory,
            &wasSelected
        );
        if (!result)
        {
            m_errorMessage = "フォルダ選択ダイアログを開けませんでした。";
            return false;
        }

        if (!wasSelected)
        {
            return false;
        }

        if (!validate_project_directory(selectedDirectory))
        {
            return false;
        }

        m_projectPath = selectedDirectory;
        m_errorMessage.clear();
        m_isOpen = false;
        return true;
    }

    bool ProjectHub::validate_project_directory(const std::string& a_projectPath)
    {
        const Core::IO::Path projectPath(a_projectPath);

        bool exists = false;
        Result result = m_fileSystem.exists(projectPath, &exists);
        if (!result)
        {
            m_errorMessage = "プロジェクトフォルダの確認に失敗しました。";
            return false;
        }

        if (!exists)
        {
            m_errorMessage = "指定したプロジェクトフォルダが存在しません。";
            return false;
        }

        Core::IO::FileStat directoryStat{};
        result = m_fileSystem.stat(projectPath, &directoryStat);
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

        const Core::IO::Path projectFilePath = Core::IO::Path::join(
            projectPath,
            Core::IO::Path("cueproject.json"));

        bool projectFileExists = false;
        result = m_fileSystem.exists(projectFilePath, &projectFileExists);
        if (!result)
        {
            m_errorMessage = "cueproject.json の確認に失敗しました。";
            return false;
        }

        if (!projectFileExists)
        {
            m_errorMessage = "cueproject.json が見つかりません。";
            return false;
        }

        Core::IO::FileStat projectFileStat{};
        result = m_fileSystem.stat(projectFilePath, &projectFileStat);
        if (!result)
        {
            m_errorMessage = "cueproject.json の情報取得に失敗しました。";
            return false;
        }

        if (projectFileStat.type != Core::IO::FileType::regular)
        {
            m_errorMessage = "cueproject.json がファイルではありません。";
            return false;
        }

        return true;
    }

    bool ProjectHub::create_project()
    {
        const std::string projectName = trim_text(m_projectNameBuffer.data());
        const std::string baseDirectory = trim_text(m_projectDirectoryBuffer.data());

        std::string projectPath{};
        const Result result = m_projectGenerator.generate(
            ProjectGenerationRequest{
                projectName,
                baseDirectory
            },
            projectPath);
        if (!result)
        {
            m_errorMessage = result.message;
            return false;
        }

        m_projectPath = projectPath;
        m_errorMessage.clear();
        m_isOpen = false;
        return true;
    }

    std::string ProjectHub::trim_text(const char* a_text) const
    {
        if (a_text == nullptr)
        {
            return "";
        }

        std::string text = a_text;
        const size_t first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
        {
            return "";
        }

        const size_t last = text.find_last_not_of(" \t\r\n");
        return text.substr(first, (last - first) + 1);
    }
}
