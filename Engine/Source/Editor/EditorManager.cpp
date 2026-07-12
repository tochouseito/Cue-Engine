#include "EditorManager.h"

// === Base includes ===
#include <CueAssert.h>
#include <CueResult.h>

// === Editor includes ===
#include "DebugCamera.h"
#include "Project/EditorProject.h"
#include "Project/ProjectSelector.h"
#include "Scene/EditorSceneManager.h"
#include "Workspace/AssetBrowser.h"
#include "Workspace/DebugView.h"
#include "Workspace/GameView.h"
#include "Workspace/Hierarchy.h"
#include "Workspace/Inspector.h"

// === Runtime includes ===
#include <CQRS/CQRS.h>
#include <Command/Commands.h>
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
        CUE_ASSERT_MSG(a_info.debugCamera != nullptr,
                       "EditorManager: debug camera is null");
        CUE_ASSERT_MSG(a_info.dialogService != nullptr,
                       "EditorManager: dialog service is null");
        CUE_ASSERT_MSG(a_info.fileSystem != nullptr,
                       "EditorManager: file system is null");

        m_backend = a_info.backend;
        m_engine = a_info.engine;
        m_debugCamera = a_info.debugCamera;
        m_dialogService = a_info.dialogService;
        m_gameCommandBridge = a_info.gameCommandBridge;

        // CueEngine と同じく、EditorManager が Editor View の所有と更新順を集約する。
        m_gameView = std::make_unique<GameView>(m_backend);
        m_debugView = std::make_unique<DebugView>(m_backend);
        m_assetBrowser =
            std::make_unique<AssetBrowser>(*a_info.fileSystem, &m_selectedAsset);
        if (m_engine != nullptr)
        {
            m_hierarchy = std::make_unique<Hierarchy>(
                m_gameCommandBridge, &m_engine->game_world(), &m_selectedEntityId,
                &m_selectedSceneId, &m_selectedAsset);
            m_inspector = std::make_unique<Inspector>(
                m_gameCommandBridge, &m_engine->game_world(), m_engine->mesh_pool(),
                &m_selectedEntityId, &m_selectedAsset);
        }

        m_project = std::make_unique<EditorProject>(*a_info.fileSystem);
        if (m_engine != nullptr)
        {
            m_sceneManager = std::make_unique<EditorSceneManager>(
                *a_info.fileSystem, m_engine->game_world(), m_engine->game_render_camera_selection(),
                m_gameCommandBridge);
        }
        m_projectSelector = std::make_unique<ProjectSelector>(*a_info.dialogService,
                                                              *a_info.fileSystem);
        m_projectSelector->open_from_executable_directory();
    }

    void EditorManager::update()
    {
        draw_dockspace();
        update_project_selector();
        draw_scene_transition_dialog();
        const bool isPlaying = m_engine != nullptr && m_engine->is_playing();
        if (m_assetBrowser != nullptr)
        {
            ImGui::BeginDisabled(isPlaying);
            if (m_project != nullptr)
            {
                m_assetBrowser->set_asset_root_path(m_project->asset_root_path());
            }
            if (m_sceneManager != nullptr)
            {
                m_assetBrowser->set_current_scene_path(
                    m_sceneManager->current_scene_path());
            }

            prepare_window_focus("Asset Browser");
            m_assetBrowser->update();

            Core::IO::Path scenePath{};
            if (m_assetBrowser->consume_open_scene_request(scenePath))
            {
                request_open_scene(scenePath);
            }

            Core::IO::Path sceneDirectory{};
            if (m_assetBrowser->consume_new_scene_request(sceneDirectory))
            {
                request_new_scene(sceneDirectory);
            }

            AssetSelection assetSelection{};
            if (m_assetBrowser->consume_asset_selection(assetSelection))
            {
                select_asset(assetSelection);
            }
            ImGui::EndDisabled();
        }
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
            ImGui::BeginDisabled(isPlaying);
            m_hierarchy->update();
            ImGui::EndDisabled();
        }
        if (m_engine != nullptr && m_inspector != nullptr)
        {
            m_inspector->set_game_world(&m_engine->game_world());
            m_inspector->set_mesh_pool(m_engine->mesh_pool());
            prepare_window_focus("インスペクター");
            ImGui::BeginDisabled(isPlaying);
            m_inspector->update();
            ImGui::EndDisabled();
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
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

        ImGui::Begin("DockSpace Window", nullptr, windowFlags);
        ImGui::PopStyleVar(2);

        draw_menu_bar();

        // fullscreen host window ではなく、この DockSpace node を docking target
        // にする
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
        if (ImGui::BeginMenu("Edit"))
        {
            draw_edit_menu_items();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("追加"))
        {
            draw_add_menu_items();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Game"))
        {
            draw_game_menu_items();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View"))
        {
            draw_view_menu_items();
            ImGui::EndMenu();
        }

        process_edit_shortcuts();
        ImGui::EndMenuBar();
    }

    void EditorManager::draw_file_menu_items()
    {
        const bool canChangeScene = m_engine == nullptr || !m_engine->is_playing();
        const bool canCreateScene =
            canChangeScene && m_project != nullptr && !m_project->root_path().is_empty();
        if (ImGui::MenuItem("新規 Scene", nullptr, false, canCreateScene))
        {
            request_scene_transition(SceneTransition::newScene);
        }

        const bool canSaveScene =
            m_sceneManager != nullptr && m_sceneManager->is_dirty();
        if (ImGui::MenuItem("保存", nullptr, false, canSaveScene))
        {
            bool isSaved = false;
            const Result result = save_current_scene(isSaved);
            if (!result)
            {
                show_scene_error(result);
            }
        }

        const bool canSaveSceneAs =
            m_sceneManager != nullptr && m_sceneManager->has_scene();
        if (ImGui::MenuItem("名前を付けて保存...", nullptr, false, canSaveSceneAs))
        {
            bool isSaved = false;
            const Result result = save_scene_as(false, isSaved);
            if (!result)
            {
                show_scene_error(result);
            }
        }

        if (ImGui::MenuItem("プロジェクト選択...", nullptr, false, canChangeScene))
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
                m_sceneManager->has_save_path()
                    ? m_sceneManager->current_scene_path().filename()
                    : m_sceneManager->scene_name();
            ImGui::TextDisabled("%s%s", m_sceneManager->is_dirty() ? "* " : "",
                                sceneName.c_str());
        }
    }

    void EditorManager::draw_add_menu_items()
    {
        const bool canEditScene = m_engine == nullptr || !m_engine->is_playing();
        if (ImGui::MenuItem("空の GameObject", nullptr, false, canEditScene))
        {
            submit_empty_object_command();
        }
    }

    void EditorManager::draw_game_menu_items()
    {
        const bool hasEngine = m_engine != nullptr;
        const bool isPlaying = hasEngine && m_engine->is_playing();
        const bool isPaused = hasEngine && m_engine->is_play_paused();

        if (ImGui::MenuItem("Play", nullptr, isPlaying && !isPaused,
                            hasEngine && !isPlaying))
        {
            const Result result = m_engine->request_start_play();
            if (!result)
            {
                show_scene_error(result);
            }
        }
        if (ImGui::MenuItem("Pause", nullptr, isPaused,
                            hasEngine && isPlaying && !isPaused))
        {
            const Result result = m_engine->request_pause_play();
            if (!result)
            {
                show_scene_error(result);
            }
        }
        if (ImGui::MenuItem("Resume", nullptr, false, hasEngine && isPaused))
        {
            const Result result = m_engine->request_resume_play();
            if (!result)
            {
                show_scene_error(result);
            }
        }
        if (ImGui::MenuItem("Step", nullptr, false, hasEngine && isPaused))
        {
            const Result result = m_engine->request_step_play();
            if (!result)
            {
                show_scene_error(result);
            }
        }
        if (ImGui::MenuItem("Stop", nullptr, false, hasEngine && isPlaying))
        {
            const Result result = m_engine->request_stop_play();
            if (!result)
            {
                show_scene_error(result);
            }
        }
    }

    void EditorManager::draw_edit_menu_items()
    {
        const bool canUndo =
            (m_engine == nullptr || !m_engine->is_playing()) &&
            m_gameCommandBridge != nullptr && m_gameCommandBridge->can_undo();
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, canUndo))
        {
            request_undo();
        }

        const bool canRedo =
            (m_engine == nullptr || !m_engine->is_playing()) &&
            m_gameCommandBridge != nullptr && m_gameCommandBridge->can_redo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, canRedo))
        {
            request_redo();
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
        if (ImGui::MenuItem("Asset Browser"))
        {
            show_and_focus_window("Asset Browser");
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

        if (!ImGui::BeginPopupModal("未保存の変更", nullptr,
                                    ImGuiWindowFlags_AlwaysAutoResize))
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
            m_pendingScenePath = {};
            m_pendingSceneDirectory = {};
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void EditorManager::request_scene_transition(
        SceneTransition a_transition, const Core::IO::Path& a_projectRoot)
    {
        if (a_transition == SceneTransition::none)
        {
            return;
        }

        // Play 中は Editor World と runtime World の対応を維持するため、終了以外の遷移を受け付けない
        if (a_transition != SceneTransition::exit &&
            m_engine != nullptr && m_engine->is_playing())
        {
            return;
        }

        m_pendingSceneTransition = a_transition;
        m_pendingProjectRoot = a_projectRoot;
        if (m_sceneManager != nullptr && m_sceneManager->is_dirty())
        {
            // Project 切替や終了で未保存 World
            // を失わないよう、実行を確認ダイアログの応答まで保留する。
            m_shouldOpenSceneTransitionDialog = true;
            return;
        }

        apply_scene_transition();
    }

    void EditorManager::request_new_scene(const Core::IO::Path& a_directory)
    {
        m_pendingSceneDirectory = a_directory;
        request_scene_transition(SceneTransition::newScene);
    }

    void EditorManager::request_open_scene(const Core::IO::Path& a_path)
    {
        if (a_path.is_empty())
        {
            return;
        }

        m_pendingScenePath = a_path;
        request_scene_transition(SceneTransition::openScene);
    }

    void EditorManager::select_asset(const AssetSelection& a_selection) noexcept
    {
        m_selectedAsset = a_selection;

        // Asset と GameObject の選択を排他的にし、Inspector の編集対象を混在させない
        m_selectedEntityId = GameCore::k_invalidEntityId;
        m_selectedSceneId = GameCore::k_invalidSceneId;
    }

    void EditorManager::apply_scene_transition()
    {
        // 遷移処理が dialog 表示や Project
        // 読み込みを再入させても同じ要求を二重実行しないよう、先に待機状態を消費する。
        const SceneTransition transition = m_pendingSceneTransition;
        const Core::IO::Path projectRoot = m_pendingProjectRoot;
        const Core::IO::Path scenePath = m_pendingScenePath;
        const Core::IO::Path sceneDirectory = m_pendingSceneDirectory;
        m_pendingSceneTransition = SceneTransition::none;
        m_pendingProjectRoot = {};
        m_pendingScenePath = {};
        m_pendingSceneDirectory = {};

        switch (transition)
        {
        case SceneTransition::newScene: {
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

            m_newSceneSaveDirectory = sceneDirectory;
            clear_selection();
            return;
        }
        case SceneTransition::openScene: {
            if (m_sceneManager == nullptr)
            {
                return;
            }

            const Result result = m_sceneManager->open_scene(scenePath);
            clear_selection();
            if (!result)
            {
                show_scene_error(result);
            }
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

        m_newSceneSaveDirectory = {};

        // Asset 解決の基準は Runtime 側の処理でも使うため、Project 読み込み後に
        // Engine へ共有する
        if (m_engine != nullptr)
        {
            m_engine->set_asset_root_path(m_project->asset_root_path());
        }

        if (m_sceneManager == nullptr)
        {
            return;
        }

        const Result sceneResult =
            m_sceneManager->open_scene(m_project->startup_scene_path());

        // Scene 読み込みは GameWorld を置き換えるため、以前の Entity / Scene
        // 選択は無効になる。
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
            return Result::fail(Code::InvalidState, Severity::Warning,
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
        if (m_sceneManager == nullptr || m_dialogService == nullptr ||
            !m_sceneManager->has_scene())
        {
            return Result::fail(Code::InvalidState, Severity::Warning,
                                "Editor scene save dialog is not initialized.");
        }

        const bool hasSavePath = m_sceneManager->has_save_path();
        Core::IO::Path initialDirectory{};
        if (hasSavePath)
        {
            initialDirectory = m_sceneManager->current_scene_path().parent();
        }
        else if (!m_newSceneSaveDirectory.is_empty())
        {
            initialDirectory = m_newSceneSaveDirectory;
        }
        else if (m_project != nullptr)
        {
            initialDirectory = m_project->asset_root_path();
        }

        const std::string defaultFileName =
            m_sceneManager->scene_name() + ".cuescene";
        PAL::SaveFileDialogDesc desc{};
        desc.title = "Scene を保存";
        desc.defaultFileName = defaultFileName.c_str();
        desc.defaultExtension = "cuescene";
        desc.filterName = "Cue Scene (*.cuescene)";
        desc.filterPattern = "*.cuescene";
        desc.initialDirectory = initialDirectory;

        Core::IO::Path selectedPath{};
        bool isSelected = false;
        Result result =
            m_dialogService->save_file_dialog(desc, selectedPath, isSelected);
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
            // 新規 Scene の初回保存だけを次回起動対象にし、既存 Scene の Save As で
            // Project 設定を意図せず変更しない。
            result = m_project->set_startup_scene_path(selectedPath);
            if (!result)
            {
                return result;
            }
        }

        if (!hasSavePath)
        {
            m_newSceneSaveDirectory = {};
        }

        if (m_assetBrowser != nullptr)
        {
            result = m_assetBrowser->refresh();
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
        Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                      "Editor scene operation failed: %s", a_result.message.data());
        if (m_projectSelector != nullptr)
        {
            m_projectSelector->show_error(a_result.message);
        }
    }

    void EditorManager::clear_selection() noexcept
    {
        m_selectedEntityId = GameCore::k_invalidEntityId;
        m_selectedSceneId = GameCore::k_invalidSceneId;
        m_selectedAsset = {};
    }

    void EditorManager::submit_empty_object_command()
    {
        if (m_gameCommandBridge == nullptr ||
            (m_engine != nullptr && m_engine->is_playing()))
        {
            return;
        }

        // GameWorld の変更は Engine の command drain に集約し、描画更新と同じ frame
        // 境界で反映する
        (void)m_gameCommandBridge->submit_command(
            std::make_unique<CreateObjectCommand>("GameObject"));
    }

    void EditorManager::process_edit_shortcuts()
    {
        if (m_engine != nullptr && m_engine->is_playing())
        {
            return;
        }

        const ImGuiIO& io = ImGui::GetIO();
        if (io.WantTextInput || !io.KeyCtrl)
        {
            return;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
        {
            if (io.KeyShift)
            {
                request_redo();
                return;
            }

            request_undo();
            return;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
        {
            request_redo();
        }
    }

    void EditorManager::request_undo()
    {
        if (m_gameCommandBridge == nullptr ||
            (m_engine != nullptr && m_engine->is_playing()))
        {
            return;
        }

        const Result result = m_gameCommandBridge->request_undo();
        if (!result)
        {
            show_scene_error(result);
        }
    }

    void EditorManager::request_redo()
    {
        if (m_gameCommandBridge == nullptr ||
            (m_engine != nullptr && m_engine->is_playing()))
        {
            return;
        }

        const Result result = m_gameCommandBridge->request_redo();
        if (!result)
        {
            show_scene_error(result);
        }
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

        // 対象 window の Begin 直前に指定すると、dock tab
        // の選択切り替えにも反映されやすい。
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

        // Window がこの frame で生成された後に focus することで、dock tab
        // の奥に隠れた場合も前面へ出す。
        ImGui::SetWindowCollapsed(m_pendingFocusWindowName.c_str(), false);
        ImGui::SetWindowFocus(m_pendingFocusWindowName.c_str());
        m_pendingFocusWindowName.clear();
    }
} // namespace Cue::Editor
