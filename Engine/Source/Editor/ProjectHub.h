#pragma once

// === Engine includes ===
#include <FrameController.h>

// === C++ includes ===
#include <cstdint>
#include <memory>
#include <deque>

// === ImGui includes ===
#include <imgui.h>

namespace Cue::Editor
{
    class ProjectHub final
    {
    public:
        ProjectHub() = default;
        ~ProjectHub() = default;

        void update()
        {
            ImGui::Begin("Project Hub"); // ウィンドウ開始
            if (ImGui::Button("New Project"))
            {
                m_isOpen = false; // 新しいプロジェクトを作成するために Project Hub を閉じる
            }
            ImGui::End();
        }

        bool is_open() const
        {
            return m_isOpen;
        }
    private:
        bool m_isOpen = true;
    };
}
