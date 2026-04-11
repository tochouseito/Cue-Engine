#pragma once

// === Engine includes ===
#include <Commands.h>

// === C++ includes ===
#include <cstdint>
#include <memory>
#include <deque>

// === ImGui includes ===
#include <imgui.h>

namespace Cue::Editor
{
    class Hierarchy final
    {
    public:
        Hierarchy(Core::CQRS::Bridge* bridge)
            : editorBridge(bridge)
        {
        }
        ~Hierarchy() = default;
        void update()
        {
            ImGui::Begin("ヒエラルキー");
            ImGui::End();
        }
    private:
        Core::CQRS::Bridge* editorBridge = nullptr;
    };
}
