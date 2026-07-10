#include "ProjectSelector.h"

// === Editor includes ===
#include "ProjectSettings.h"

// === Engine includes ===
#include <GameCore/SceneAsset.h>
#include <GameCore/SceneSerializer.h>

// === Runtime includes ===
#include <Dialog/DialogService.h>
#include <IO/IFileSystem.h>

// === C++ includes ===
#include <algorithm>
#include <string>

// === ImGui includes ===
#include <imgui.h>

namespace Cue::Editor
{
    namespace
    {
        constexpr const char* k_projectFileName = "cueproject.json";
        constexpr const char* k_defaultScenePath = "Assets/Scenes/Main.cuescene";

        [[nodiscard]] bool is_valid_project_name(std::string_view a_name) noexcept
        {
            return !a_name.empty() &&
                   a_name != "." &&
                   a_name != ".." &&
                   a_name.find_first_of("\\/:*?\"<>|") == std::string_view::npos;
        }
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
        if (ImGui::Button("プロジェクトを新規作成", ImVec2(-1.0f, 0.0f)))
        {
            begin_create_project();
        }

        if (!m_errorMessage.empty())
        {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 96, 96, 255));
            ImGui::TextWrapped("%s", m_errorMessage.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::End();

        draw_create_project_dialog();
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

    void ProjectSelector::begin_create_project()
    {
        m_createParentDirectory = m_initialDirectory;
        std::fill(m_projectName.begin(), m_projectName.end(), '\0');
        m_isCreateProjectDialogOpen = true;
    }

    void ProjectSelector::draw_create_project_dialog()
    {
        if (m_isCreateProjectDialogOpen)
        {
            ImGui::OpenPopup("プロジェクトを新規作成");
            m_isCreateProjectDialogOpen = false;
        }

        if (!ImGui::BeginPopupModal("プロジェクトを新規作成", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        ImGui::InputText("名前", m_projectName.data(), m_projectName.size());
        ImGui::TextUnformatted("保存先");
        ImGui::TextWrapped("%s", m_createParentDirectory.utf8().c_str());
        if (ImGui::Button("保存先を選択"))
        {
            select_project_parent_directory();
        }

        ImGui::Separator();
        if (ImGui::Button("作成"))
        {
            const Result result = create_project();
            if (result)
            {
                ImGui::CloseCurrentPopup();
            }
            else
            {
                m_errorMessage = result.message;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("キャンセル"))
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void ProjectSelector::select_project_parent_directory()
    {
        if (m_dialogService == nullptr)
        {
            m_errorMessage = "DialogService が初期化されていません。";
            return;
        }

        PAL::FolderDialogDesc desc{};
        desc.title = "プロジェクトの保存先を選択";
        desc.initialDirectory = m_createParentDirectory;

        Core::IO::Path selectedPath{};
        bool isSelected = false;
        const Result result = m_dialogService->open_folder_dialog(desc, selectedPath, isSelected);
        if (!result)
        {
            m_errorMessage = "プロジェクトの保存先を選択できませんでした。";
            return;
        }
        if (isSelected)
        {
            m_createParentDirectory = selectedPath.normalize();
        }
    }

    Result ProjectSelector::create_project()
    {
        if (m_fileSystem == nullptr)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "Project file system is not initialized.");
        }
        if (m_createParentDirectory.is_empty())
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Warning,
                "Project parent directory is empty.");
        }

        const std::string projectName(m_projectName.data());
        if (!is_valid_project_name(projectName))
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Warning,
                "Project name is invalid.");
        }

        const Core::IO::Path projectRoot =
            Core::IO::Path::join(m_createParentDirectory, Core::IO::Path(projectName));
        bool alreadyExists = false;
        Result result = m_fileSystem->exists(projectRoot, &alreadyExists);
        if (!result)
        {
            return result;
        }
        if (alreadyExists)
        {
            return Result::fail(
                Code::CreateFailed,
                Severity::Warning,
                "Project directory already exists.");
        }

        const Core::IO::Path assetRoot = Core::IO::Path::join(projectRoot, Core::IO::Path("Assets"));
        const Core::IO::Path sceneDirectory = Core::IO::Path::join(assetRoot, Core::IO::Path("Scenes"));
        const Core::IO::Path materialDirectory = Core::IO::Path::join(assetRoot, Core::IO::Path("Materials"));
        const Core::IO::Path modelDirectory = Core::IO::Path::join(assetRoot, Core::IO::Path("Models"));
        const Core::IO::Path scriptDirectory = Core::IO::Path::join(assetRoot, Core::IO::Path("Scripts"));
        const Core::IO::Path textureDirectory = Core::IO::Path::join(assetRoot, Core::IO::Path("Textures"));
        const Core::IO::Path savedDirectory = Core::IO::Path::join(projectRoot, Core::IO::Path("Saved"));

        const Core::IO::Path directories[] = {
            sceneDirectory,
            materialDirectory,
            modelDirectory,
            scriptDirectory,
            textureDirectory,
            savedDirectory};
        for (const Core::IO::Path& directory : directories)
        {
            result = m_fileSystem->create_directories(directory);
            if (!result)
            {
                return result;
            }
        }

        GameCore::SceneAsset scene{};
        scene.name = "Main";
        const Core::IO::Path scenePath = Core::IO::Path::join(projectRoot, Core::IO::Path(k_defaultScenePath));
        result = GameCore::save_scene_asset(*m_fileSystem, scenePath, scene);
        if (!result)
        {
            return result;
        }

        ProjectSettings settings{};
        settings.name = projectName;
        settings.startupScene = k_defaultScenePath;
        settings.root = projectRoot;
        settings.assetRoot = assetRoot;
        result = save_project_settings(*m_fileSystem, settings);
        if (!result)
        {
            return result;
        }

        m_initialDirectory = m_createParentDirectory;
        m_selectedRoot = projectRoot.normalize();
        m_hasSelectedProject = true;
        m_isOpen = false;
        m_errorMessage.clear();
        return Result::ok();
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
