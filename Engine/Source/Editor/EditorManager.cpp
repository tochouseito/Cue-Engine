#include "EditorManager.h"

// === Base includes ===
#include <CueAssert.h>

// === Editor includes ===
#include "DebugCamera.h"
#include "Hierarchy.h"
#include "Inspector.h"
#include "Project/ProjectSelector.h"
#include "Project/ProjectSettings.h"
#include "Workspace/DebugView.h"
#include "Workspace/Dockspace.h"
#include "Workspace/GameView.h"

// === Runtime includes ===
#include <Command/Commands.h>
#include <CQRS/CQRS.h>
#include <Engine.h>

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
        CUE_ASSERT_MSG(a_info.fileSystem != nullptr, "EditorManager: file system is null");

        m_backend = a_info.backend;
        m_engine = a_info.engine;
        m_debugCamera = a_info.debugCamera;
        m_fileSystem = a_info.fileSystem;
        m_gameCommandBridge = a_info.gameCommandBridge;

        // CueEngine と同じく、EditorManager が Editor View の所有と更新順を集約する。
        m_dockspace = std::make_unique<Dockspace>();
        m_dockspace->set_file_menu_callback(
            this,
            [](void* a_context)
            {
                static_cast<EditorManager*>(a_context)->draw_file_menu_items();
            });
        m_dockspace->set_view_menu_callback(
            this,
            [](void* a_context)
            {
                static_cast<EditorManager*>(a_context)->draw_view_menu_items();
            });
        m_dockspace->set_add_menu_callback(
            this,
            [](void* a_context)
            {
                static_cast<EditorManager*>(a_context)->draw_add_menu_items();
            });
        m_gameView = std::make_unique<GameView>(m_backend);
        m_debugView = std::make_unique<DebugView>(m_backend);
        if (m_engine != nullptr)
        {
            m_hierarchy = std::make_unique<Hierarchy>(
                m_gameCommandBridge, &m_engine->game_world(), &m_selectedEntityId, &m_selectedSceneId);
            m_inspector = std::make_unique<Inspector>(
                m_gameCommandBridge, &m_engine->game_world(), m_engine->mesh_pool(), &m_selectedEntityId);
        }

        m_projectSelector = std::make_unique<ProjectSelector>(*m_fileSystem);
        m_projectSelector->open_from_executable_directory();
    }

    Result EditorManager::load_project(const Core::IO::Path& a_root)
    {
        if (m_fileSystem == nullptr || m_engine == nullptr)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "EditorManager project dependencies are not initialized.");
        }

        ProjectSettings settings{};
        Result result = load_project_settings(*m_fileSystem, a_root, settings);
        if (!result)
        {
            return result;
        }

        m_projectRootPath = settings.root;
        m_assetRootPath = settings.assetRoot;
        m_projectName = settings.name;
        m_startupScene = settings.startupScene;

        // Asset 解決の基準は Runtime 側の処理でも使うため、Project 読み込み時点で Engine に共有する
        m_engine->set_asset_root_path(m_assetRootPath);
        return Result::ok();
    }

    void EditorManager::update()
    {
        if (m_dockspace != nullptr)
        {
            m_dockspace->update();
        }
        update_project_selector();
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

    void EditorManager::draw_file_menu_items()
    {
        if (ImGui::MenuItem("プロジェクト選択..."))
        {
            open_project_selector();
        }

        if (!m_projectName.empty())
        {
            ImGui::Separator();
            ImGui::TextDisabled("%s", m_projectName.c_str());
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

        const Result result = load_project(selectedProjectRoot);
        if (result)
        {
            return;
        }

        Core::IO::log(
            Core::IO::LogSink::console | Core::IO::LogSink::file,
            "Failed to load project: %s",
            result.message.data());
        m_projectSelector->show_error(result.message);
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
