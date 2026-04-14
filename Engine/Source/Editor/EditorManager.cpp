#include "EditorManager.h"

// === Base includes ===
#include <CueAssert.h>

namespace Cue::Editor
{
    void EditorManager::initialize()
    {
        m_statistics = std::make_unique<Statistics>(m_engine->frame_controller());
        m_debugView = std::make_unique<DebugView>(m_backend, m_bridge);
        m_hierarchy = std::make_unique<Hierarchy>(m_bridge, m_engine->game_world());
        m_inspector = std::make_unique<Inspector>(m_bridge);
    }

    void EditorManager::undo_last_command()
    {
        if (m_bridge == nullptr || m_engine == nullptr || m_engine->game_world() == nullptr)
        {
            return;
        }

        EngineCommandContext commandContext(*m_engine->game_world());
        Result result = m_bridge->undo_last_command(commandContext);
        if (!result && result.code != Code::InvalidState)
        {
            CUE_ASSERTF(false,
                "Failed to undo command: %s (code: %s, severity: %s) at %s:%u in function %s",
                result.message.data(), Cue::to_string(result.code),
                Cue::to_string(result.severity), result.file,
                result.line, result.function);
        }
    }

    void EditorManager::redo_last_command()
    {
        if (m_bridge == nullptr || m_engine == nullptr || m_engine->game_world() == nullptr)
        {
            return;
        }

        EngineCommandContext commandContext(*m_engine->game_world());
        Result result = m_bridge->redo_last_command(commandContext);
        if (!result && result.code != Code::InvalidState)
        {
            CUE_ASSERTF(false,
                "Failed to redo command: %s (code: %s, severity: %s) at %s:%u in function %s",
                result.message.data(), Cue::to_string(result.code),
                Cue::to_string(result.severity), result.file,
                result.line, result.function);
        }
    }

    void EditorManager::handle_shortcuts()
    {
        if (m_bridge == nullptr)
        {
            return;
        }

        const ImGuiIO& io = ImGui::GetIO();
        if (io.WantTextInput)
        {
            return;
        }

        if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false))
        {
            undo_last_command();
        }

        if (io.KeyCtrl &&
            (ImGui::IsKeyPressed(ImGuiKey_Y, false) ||
                (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false))))
        {
            redo_last_command();
        }
    }

    void EditorManager::update()
    {
        // ビューポート全体をカバーするドックスペースを作成
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos); // 次のウィンドウの位置をメインビューポートの位置に設定
        ImGui::SetNextWindowSize(viewport->Size); // 次のウィンドウのサイズをメインビューポートのサイズに設定
        ImGui::SetNextWindowViewport(viewport->ID); // ビューポートIDをメインビューポートに設定

        // タイトルバーを削除し、リサイズや移動を防止し、背景のみとするウィンドウフラグを設定
        ImGuiWindowFlags window_flags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_MenuBar;

        // ウィンドウの丸みとボーダーをなくして、シームレスなドッキング外観にする
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

        ImGui::Begin("DockSpace Window", nullptr, window_flags); // ドックスペースとして機能する新しいウィンドウを開始
        ImGui::PopStyleVar(2); // 先ほどプッシュしたスタイル変数を2つポップする

        static bool showMetricsWindow = false;
        static bool showDemoWindow = false;
        static bool showStyleEditor = false;

        handle_shortcuts();

        // メニューバー
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("編集"))
            {
                const bool canUndo = m_bridge != nullptr && m_bridge->can_undo();
                const bool canRedo = m_bridge != nullptr && m_bridge->can_redo();

                if (ImGui::MenuItem("Undo", "Ctrl+Z", false, canUndo))
                {
                    undo_last_command();
                }

                if (ImGui::MenuItem("Redo", "Ctrl+Y", false, canRedo))
                {
                    redo_last_command();
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Test"))
            {
                if (ImGui::MenuItem("Show Metrics Window"))
                {
                    showMetricsWindow = !showMetricsWindow;
                }

                if (ImGui::MenuItem("Show Demo Window"))
                {
                    showDemoWindow = !showDemoWindow;
                }

                if (ImGui::MenuItem("Show Style Editor"))
                {
                    showStyleEditor = !showStyleEditor;
                }

                ImGui::EndMenu();
            }

            ImGui::EndMenuBar(); // メニューバーを終了
        }

        // ウィンドウ内にドックスペースを作成
        ImGuiID dockspace_id = ImGui::GetID("MyDockspace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
        ImGui::End(); // ドックスペースウィンドウを終了

        if(showMetricsWindow)
        {
            ImGui::ShowMetricsWindow(&showMetricsWindow);
        }
        if(showDemoWindow)
        {
            ImGui::ShowDemoWindow(&showDemoWindow);
        }
        if (showStyleEditor)
        {
            ImGui::ShowStyleEditor();
        }

        m_statistics->update();
        m_debugView->update();
        m_hierarchy->update();
        m_inspector->update();
    }
}
