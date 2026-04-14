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
#include <span>

// === ThirdParty includes ===
#include <nlohmann/json.hpp>

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

    bool ProjectHub::create_project()
    {
        const std::string projectName = trim_text(m_projectNameBuffer.data());
        const std::string baseDirectory = trim_text(m_projectDirectoryBuffer.data());

        if (projectName.empty())
        {
            m_errorMessage = "プロジェクト名を入力してください。";
            return false;
        }

        if (baseDirectory.empty())
        {
            m_errorMessage = "作成先ディレクトリを指定してください。";
            return false;
        }

        if (has_invalid_project_name_character(projectName))
        {
            m_errorMessage =
                "プロジェクト名に使用できない文字が含まれています。";
            return false;
        }

        const Core::IO::Path basePath(baseDirectory);
        bool baseExists = false;
        Result result = m_fileSystem.exists(basePath, &baseExists);
        if (!result)
        {
            m_errorMessage =
                "作成先ディレクトリの確認に失敗しました。";
            return false;
        }

        if (!baseExists)
        {
            m_errorMessage =
                "作成先ディレクトリが存在しないか、ディレクトリではありません。";
            return false;
        }

        Core::IO::FileStat baseStat{};
        result = m_fileSystem.stat(basePath, &baseStat);
        if (!result)
        {
            m_errorMessage =
                "作成先ディレクトリの情報取得に失敗しました。";
            return false;
        }

        if (baseStat.type != Core::IO::FileType::directory)
        {
            m_errorMessage =
                "作成先ディレクトリが存在しないか、ディレクトリではありません。";
            return false;
        }

        const Core::IO::Path projectPath =
            Core::IO::Path::join(basePath, Core::IO::Path(projectName));
        bool projectExists = false;
        result = m_fileSystem.exists(projectPath, &projectExists);
        if (!result)
        {
            m_errorMessage =
                "作成先プロジェクトパスの確認に失敗しました。";
            return false;
        }

        if (projectExists)
        {
            m_errorMessage = "同名のフォルダがすでに存在します。";
            return false;
        }

        result = m_fileSystem.create_directories(projectPath);
        if (!result)
        {
            m_errorMessage =
                "プロジェクトフォルダの作成に失敗しました。";
            return false;
        }

        if (!write_project_file(projectName, projectPath.utf8()))
        {
            return false;
        }

        m_projectPath = projectPath.utf8();
        m_errorMessage.clear();
        m_isOpen = false;
        return true;
    }

    bool ProjectHub::write_project_file(
        const std::string& a_projectName,
        const std::string& a_projectPath
    )
    {
        nlohmann::json projectJson = {
            { "name", a_projectName },
            { "engineVersion", 1 },
            { "assetRoot", "Assets" },
            { "scriptRoot", "." },
            { "startupScene", "Assets/Scenes/Main.cuescene" }
        };

        std::string jsonText = projectJson.dump(4);
        jsonText.push_back('\n');

        const Core::IO::Path projectFilePath = Core::IO::Path::join(
            Core::IO::Path(a_projectPath),
            Core::IO::Path("cueproject.json"));

        const std::span<const char> textSpan(jsonText.data(), jsonText.size());
        const std::span<const std::byte> byteSpan = std::as_bytes(textSpan);
        const Result result =
            m_fileSystem.write_all(projectFilePath, byteSpan, false);
        if (!result)
        {
            m_errorMessage = "cueproject.json の書き込みに失敗しました。";
            return false;
        }

        return true;
    }

    bool ProjectHub::has_invalid_project_name_character(
        const std::string& a_projectName) const
    {
        static constexpr const char* k_invalidChars = "\\/:*?\"<>|";
        return a_projectName.find_first_of(k_invalidChars) != std::string::npos;
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
