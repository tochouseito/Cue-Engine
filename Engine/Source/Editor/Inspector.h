#pragma once

// === Engine includes ===
#include <Commands.h>

// === Editor includes ===
#include "Icon.h"

// === C++ includes ===
#include <cstdint>
#include <memory>
#include <deque>

// === ImGui includes ===
#include <imgui.h>

namespace Cue::Editor
{
    enum class PropertyTab
    {
        Render,
        Material,
        Physics,
        Script
    };

    static PropertyTab currentTab = PropertyTab::Render;

    class Inspector final
    {
    public:
        Inspector(Core::CQRS::Bridge* bridge)
            : editorBridge(bridge)
        {
        }
        ~Inspector() = default;
        void update()
        {
            ImGui::Begin("インスペクター");
            // 左カラム
            ImGui::BeginChild("TabList", ImVec2(40.0f, 0.0f), true);
            {
                if (ImGui::Selectable(CUE_ICON_INFO, currentTab == PropertyTab::Render, 0, ImVec2(32.0f, 32.0f)))
                {
                    currentTab = PropertyTab::Render;
                }

                if (ImGui::Selectable(CUE_ICON_ADD, currentTab == PropertyTab::Material, 0, ImVec2(32.0f, 32.0f)))
                {
                    currentTab = PropertyTab::Material;
                }

                if (ImGui::Selectable(CUE_ICON_BUG_REPORT, currentTab == PropertyTab::Physics, 0, ImVec2(32.0f, 32.0f)))
                {
                    currentTab = PropertyTab::Physics;
                }

                if (ImGui::Selectable(CUE_ICON_CHEVRON_RIGHT, currentTab == PropertyTab::Script, 0, ImVec2(32.0f, 32.0f)))
                {
                    currentTab = PropertyTab::Script;
                }
            }

            ImGui::EndChild();

            ImGui::SameLine(0.0f, 0.0f);

            // 右カラム
            ImGui::BeginChild("TabContent", ImVec2(0.0f, 0.0f), true);
            {
                switch (currentTab)
                {
                case PropertyTab::Render:
                    ImGui::Text("Render");
                    break;

                case PropertyTab::Material:
                    ImGui::Text("Material");
                    break;

                case PropertyTab::Physics:
                    ImGui::Text("Physics");
                    break;

                case PropertyTab::Script:
                    ImGui::Text("Script");
                    break;
                }
            }
            ImGui::EndChild();
            ImGui::End();
        }
    private:
        Core::CQRS::Bridge* editorBridge = nullptr;
    };
}
