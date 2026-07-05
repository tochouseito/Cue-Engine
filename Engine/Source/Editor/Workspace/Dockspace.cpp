#include "Dockspace.h"

// === ImGui includes ===
#include <imgui.h>

namespace Cue::Editor
{
    void Dockspace::update()
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGuiWindowFlags window_flags =
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

        ImGui::Begin("DockSpace Window", nullptr, window_flags);
        ImGui::PopStyleVar(2);

        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (m_fileMenuCallback != nullptr)
                {
                    m_fileMenuCallback(m_fileMenuContext);
                }
                if (ImGui::MenuItem("Exit"))
                {
                    // Handle exit action
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("追加"))
            {
                if (m_addMenuCallback != nullptr)
                {
                    m_addMenuCallback(m_addMenuContext);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View"))
            {
                if (m_viewMenuCallback != nullptr)
                {
                    m_viewMenuCallback(m_viewMenuContext);
                }
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }

        // fullscreen host window ではなく、この DockSpace node を docking target にする。
        const ImGuiID dockspaceId = ImGui::GetID("EditorDockSpace");
        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

        ImGui::End();
    }
}
