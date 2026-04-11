#include "EditorManager.h"

namespace Cue::Editor
{
    void EditorManager::initialize()
    {
        m_statistics = std::make_unique<Statistics>(m_engine->frame_controller());
        m_debugView = std::make_unique<DebugView>(m_backend, m_bridge);
        m_hierarchy = std::make_unique<Hierarchy>(m_bridge);
        m_inspector = std::make_unique<Inspector>(m_bridge);
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
        // メニューバー
        if (ImGui::BeginMenuBar())
        {
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
