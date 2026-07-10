#include "EditorManager.h"

// === Base includes ===
#include <CueAssert.h>
#include <CueResult.h>

// === Editor includes ===
#include "DebugCamera.h"
#include "Project/EditorProject.h"
#include "Project/ProjectSelector.h"
#include "Scene/EditorSceneManager.h"
#include "Workspace/DebugView.h"
#include "Workspace/GameView.h"
#include "Workspace/Hierarchy.h"
#include "Workspace/Inspector.h"

// === Runtime includes ===
#include <Command/Commands.h>
#include <CQRS/CQRS.h>
#include <Dialog/DialogService.h>
#include <Engine.h>
#include <IO/Path.h>

// === ImGui includes ===
#include <imgui.h>

namespace Cue::Editor
{
    EditorManager::EditorManager() = default;

    EditorManager::~EditorManager() = default;

    void EditorManager::initialize(const EditorManagerSetupInfo& a_info)
    {
        CUE_ASSERT_MSG(a_info.backend != nullptr, "EditorManager: backend is null");
        CUE_ASSERT_MSG(a_info.debugCamera != nullptr, "EditorManager: debug camera is null");
        CUE_ASSERT_MSG(a_info.dialogService != nullptr, "EditorManager: dialog service is null");
        CUE_ASSERT_MSG(a_info.fileSystem != nullptr, "EditorManager: file system is null");

        m_backend = a_info.backend;
        m_engine = a_info.engine;
        m_debugCamera = a_info.debugCamera;
        m_dialogService = a_info.dialogService;
        m_gameCommandBridge = a_info.gameCommandBridge;

        // CueEngine と同じく、EditorManager が Editor View の所有と更新順を集約する。
        m_gameView = std::make_unique<GameView>(m_backend);
        m_debugView = std::make_unique<DebugView>(m_backend);
        if (m_engine != nullptr)
        {
            m_hierarchy = std::make_unique<Hierarchy>(
                m_gameCommandBridge, &m_engine->game_world(), &m_selectedEntityId, &m_selectedSceneId);
            m_inspector = std::make_unique<Inspector>(
                m_gameCommandBridge, &m_engine->game_world(), m_engine->mesh_pool(), &m_selectedEntityId);
        }

        m_project = std::make_unique<EditorProject>(*a_info.fileSystem);
        if (m_engine != nullptr)
        {
            m_sceneManager = std::make_unique<EditorSceneManager>(*a_info.fileSystem, m_engine->game_world());
        }
        m_projectSelector = std::make_unique<ProjectSelector>(*a_info.dialogService, *a_info.fileSystem);
        m_projectSelector->open_from_executable_directory();
    }

    void EditorManager::update()
    {
        draw_dockspace();
        update_project_selector();
        draw_scene_transition_dialog();
        if (m_gameView != nullptr)
        {
            prepare_window_focus("GameView");
            m_gameView->update();
        }
        if (m_debugView != nullptr)
        {
            prepare_window_focus("DebugView");
            m_debugView->update();
        }
        if (m_engine != nullptr && m_hierarchy != nullptr)
        {
            m_hierarchy->set_game_world(&m_engine->game_world());
            prepare_window_focus("ヒエラルキー");
            m_hierarchy->update();
        }
        if (m_engine != nullptr && m_inspector != nullptr)
        {
            m_inspector->set_game_world(&m_engine->game_world());
            m_inspector->set_mesh_pool(m_engine->mesh_pool());
            prepare_window_focus("インスペクター");
            m_inspector->update();
        }
        focus_pending_window();

        if (m_debugCamera == nullptr || m_debugView == nullptr)
        {
            return;
        }

        DebugCameraViewport debugCameraViewport{};
        debugCameraViewport.width = m_debugView->viewport_width();
        debugCameraViewport.height = m_debugView->viewport_height();
        debugCameraViewport.isHovered = m_debugView->is_viewport_hovered();
        debugCameraViewport.isFocused = m_debugView->is_focused();
        m_debugCamera->update(debugCameraViewport);
    }

    bool EditorManager::request_exit()
    {
        if (m_sceneManager == nullptr || !m_sceneManager->is_dirty())
        {
            return false;
        }

        request_scene_transition(SceneTransition::exit);
        return true;
    }

    bool EditorManager::consume_exit_request() noexcept
    {
        const bool isRequested = m_isExitRequested;
        m_isExitRequested = false;
        return isRequested;
    }

    void EditorManager::draw_dockspace()
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::SetNextWindowViewport(viewport->ID);

        const ImGuiWindowFlags windowFlags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_MenuBar;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

        ImGui::Begin("DockSpace Window", nullptr, windowFlags);
        ImGui::PopStyleVar(2);

        draw_menu_bar();

        // fullscreen host window ではなく、この DockSpace node を docking target にする
        const ImGuiID dockspaceId = ImGui::GetID("EditorDockSpace");
        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

        ImGui::End();
    }

    void EditorManager::draw_menu_bar()
    {
        if (!ImGui::BeginMenuBar())
        {
            return;
        }

        if (ImGui::BeginMenu("File"))
        {
            draw_file_menu_items();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("追加"))
        {
            draw_add_menu_items();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View"))
        {
            draw_view_menu_items();
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }

    void EditorManager::draw_file_menu_items()
    {
        const bool canCreateScene = m_project != nullptr && !m_project->root_path().is_empty();
        if (ImGui::MenuItem("新規 Scene", nullptr, false, canCreateScene))
        {
            request_scene_transition(SceneTransition::newScene);
        }

        const bool canSaveScene = m_sceneManager != nullptr && m_sceneManager->is_dirty();
        if (ImGui::MenuItem("保存", nullptr, false, canSaveScene))
        {
            bool isSaved = false;
            const Result result = save_current_scene(isSaved);
            if (!result)
            {
                show_scene_error(result);
            }
        }

        const bool canSaveSceneAs = m_sceneManager != nullptr && m_sceneManager->has_scene();
        if (ImGui::MenuItem("名前を付けて保存...", nullptr, false, canSaveSceneAs))
        {
            bool isSaved = false;
            const Result result = save_scene_as(false, isSaved);
            if (!result)
            {
                show_scene_error(result);
            }
        }

        if (ImGui::MenuItem("プロジェクト選択..."))
        {
            open_project_selector();
        }

        if (m_project != nullptr && !m_project->name().empty())
        {
            ImGui::Separator();
            ImGui::TextDisabled("%s", m_project->name().c_str());
        }

        if (m_sceneManager != nullptr && m_sceneManager->has_scene())
        {
            const std::string sceneName =
                m_sceneManager->has_save_path() ? m_sceneManager->current_scene_path().filename() : m_sceneManager->scene_name();
            ImGui::TextDisabled("%s%s", m_sceneManager->is_dirty() ? "* " : "", sceneName.c_str());
        }
    }

    void EditorManager::draw_add_menu_items()
    {
        if (ImGui::MenuItem("空の GameObject"))
        {
            submit_empty_object_command();
        }
    }

    void EditorManager::draw_view_menu_items()
    {
        if (ImGui::MenuItem("GameView"))
        {
            show_and_focus_window("GameView");
        }
        if (ImGui::MenuItem("DebugView"))
        {
            show_and_focus_window("DebugView");
        }
        if (ImGui::MenuItem("ヒエラルキー"))
        {
            show_and_focus_window("ヒエラルキー");
        }
        if (ImGui::MenuItem("インスペクター"))
        {
            show_and_focus_window("インスペクター");
        }
    }

    void EditorManager::open_project_selector()
    {
        if (m_projectSelector == nullptr)
        {
            return;
        }

        m_projectSelector->open();
    }

    void EditorManager::update_project_selector()
    {
        if (m_projectSelector == nullptr)
        {
            return;
        }

        m_projectSelector->update();

        Core::IO::Path selectedProjectRoot{};
        if (!m_projectSelector->consume_selected_project(selectedProjectRoot))
        {
            return;
        }

        request_scene_transition(SceneTransition::openProject, selectedProjectRoot);
    }

    void EditorManager::draw_scene_transition_dialog()
    {
        if (m_shouldOpenSceneTransitionDialog)
        {
            ImGui::OpenPopup("未保存の変更");
            m_shouldOpenSceneTransitionDialog = false;
        }

        if (!ImGui::BeginPopupModal("未保存の変更", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        ImGui::TextUnformatted("Scene に未保存の変更があります。");
        if (ImGui::Button("保存"))
        {
            bool isSaved = false;
            const Result result = save_current_scene(isSaved);
            if (!result)
            {
                show_scene_error(result);
            }
            else if (isSaved)
            {
                ImGui::CloseCurrentPopup();
                apply_scene_transition();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("破棄"))
        {
            ImGui::CloseCurrentPopup();
            apply_scene_transition();
        }
        ImGui::SameLine();
        if (ImGui::Button("キャンセル"))
        {
            m_pendingSceneTransition = SceneTransition::none;
            m_pendingProjectRoot = {};
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void EditorManager::request_scene_transition(
        SceneTransition a_transition,
        const Core::IO::Path& a_projectRoot)
    {
        if (a_transition == SceneTransition::none)
        {
            return;
        }

        m_pendingSceneTransition = a_transition;
        m_pendingProjectRoot = a_projectRoot;
        if (m_sceneManager != nullptr && m_sceneManager->is_dirty())
        {
            // Project 切替や終了で未保存 World を失わないよう、実行を確認ダイアログの応答まで保留する。
            m_shouldOpenSceneTransitionDialog = true;
            return;
        }

        apply_scene_transition();
    }

    void EditorManager::apply_scene_transition()
    {
        // 遷移処理が dialog 表示や Project 読み込みを再入させても同じ要求を二重実行しないよう、先に待機状態を消費する。
        const SceneTransition transition = m_pendingSceneTransition;
        const Core::IO::Path projectRoot = m_pendingProjectRoot;
        m_pendingSceneTransition = SceneTransition::none;
        m_pendingProjectRoot = {};

        switch (transition)
        {
        case SceneTransition::newScene:
        {
            if (m_sceneManager == nullptr)
            {
                return;
            }

            const Result result = m_sceneManager->new_scene();
            if (!result)
            {
                show_scene_error(result);
                return;
            }

            clear_selection();
            return;
        }
        case SceneTransition::openProject:
            load_project(projectRoot);
            return;
        case SceneTransition::exit:
            m_isExitRequested = true;
            return;
        case SceneTransition::none:
            return;
        }
    }

    void EditorManager::load_project(const Core::IO::Path& a_projectRoot)
    {
        if (m_project == nullptr)
        {
            if (m_projectSelector != nullptr)
            {
                m_projectSelector->show_error("Editor project is not initialized.");
            }
            return;
        }

        const Result result = m_project->load(a_projectRoot);
        if (!result)
        {
            show_scene_error(result);
            return;
        }

        // Asset 解決の基準は Runtime 側の処理でも使うため、Project 読み込み後に Engine へ共有する
        if (m_engine != nullptr)
        {
            m_engine->set_asset_root_path(m_project->asset_root_path());
        }

        if (m_sceneManager == nullptr)
        {
            return;
        }

        const Result sceneResult = m_sceneManager->open_scene(m_project->startup_scene_path());

        // Scene 読み込みは GameWorld を置き換えるため、以前の Entity / Scene 選択は無効になる。
        clear_selection();

        if (!sceneResult)
        {
            show_scene_error(sceneResult);
        }
    }

    Result EditorManager::save_current_scene(bool& a_outSaved)
    {
        a_outSaved = false;
        if (m_sceneManager == nullptr || !m_sceneManager->has_scene())
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Warning,
                "Editor does not have a scene to save.");
        }

        if (!m_sceneManager->has_save_path())
        {
            return save_scene_as(true, a_outSaved);
        }

        const Result result = m_sceneManager->save_scene();
        if (result)
        {
            a_outSaved = true;
        }
        return result;
    }

    Result EditorManager::save_scene_as(bool a_setsStartupScene, bool& a_outSaved)
    {
        a_outSaved = false;
        if (m_sceneManager == nullptr || m_dialogService == nullptr || !m_sceneManager->has_scene())
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Warning,
                "Editor scene save dialog is not initialized.");
        }

        Core::IO::Path initialDirectory{};
        if (m_sceneManager->has_save_path())
        {
            initialDirectory = m_sceneManager->current_scene_path().parent();
        }
        else if (m_project != nullptr)
        {
            initialDirectory = m_project->asset_root_path();
        }

        const std::string defaultFileName = m_sceneManager->scene_name() + ".cuescene";
        PAL::SaveFileDialogDesc desc{};
        desc.title = "Scene を保存";
        desc.defaultFileName = defaultFileName.c_str();
        desc.defaultExtension = "cuescene";
        desc.filterName = "Cue Scene (*.cuescene)";
        desc.filterPattern = "*.cuescene";
        desc.initialDirectory = initialDirectory;

        Core::IO::Path selectedPath{};
        bool isSelected = false;
        Result result = m_dialogService->save_file_dialog(desc, selectedPath, isSelected);
        if (!result || !isSelected)
        {
            return result;
        }

        result = m_sceneManager->save_scene_as(selectedPath);
        if (!result)
        {
            return result;
        }

        if (a_setsStartupScene && m_project != nullptr)
        {
            // 新規 Scene の初回保存だけを次回起動対象にし、既存 Scene の Save As で Project 設定を意図せず変更しない。
            result = m_project->set_startup_scene_path(selectedPath);
            if (!result)
            {
                return result;
            }
        }

        a_outSaved = true;
        return Result::ok();
    }

    void EditorManager::show_scene_error(const Result& a_result)
    {
        Core::IO::log(
            Core::IO::LogSink::console | Core::IO::LogSink::file,
            "Editor scene operation failed: %s",
            a_result.message.data());
        if (m_projectSelector != nullptr)
        {
            m_projectSelector->show_error(a_result.message);
        }
    }

    void EditorManager::clear_selection() noexcept
    {
        m_selectedEntityId = GameCore::k_invalidEntityId;
        m_selectedSceneId = GameCore::k_invalidSceneId;
    }

    void EditorManager::submit_empty_object_command()
    {
        if (m_gameCommandBridge == nullptr)
        {
            return;
        }

        // GameWorld の変更は Engine の command drain に集約し、描画更新と同じ frame 境界で反映する
        (void)m_gameCommandBridge->submit_command(std::make_unique<CreateObjectCommand>("GameObject"));
    }

    void EditorManager::show_and_focus_window(const char* a_windowName)
    {
        if (a_windowName == nullptr)
        {
            return;
        }

        m_pendingFocusWindowName = a_windowName;
    }

    bool EditorManager::prepare_window_focus(const char* a_windowName)
    {
        if (a_windowName == nullptr || m_pendingFocusWindowName != a_windowName)
        {
            return false;
        }

        // 対象 window の Begin 直前に指定すると、dock tab の選択切り替えにも反映されやすい。
        ImGui::SetNextWindowCollapsed(false);
        ImGui::SetNextWindowFocus();
        m_pendingFocusWindowName.clear();
        return true;
    }

    void EditorManager::focus_pending_window()
    {
        if (m_pendingFocusWindowName.empty())
        {
            return;
        }

        // Window がこの frame で生成された後に focus することで、dock tab の奥に隠れた場合も前面へ出す。
        ImGui::SetWindowCollapsed(m_pendingFocusWindowName.c_str(), false);
        ImGui::SetWindowFocus(m_pendingFocusWindowName.c_str());
        m_pendingFocusWindowName.clear();
    }
} // namespace Cue::Editor
