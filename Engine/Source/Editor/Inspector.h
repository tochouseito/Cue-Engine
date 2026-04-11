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
            ImGui::End();
        }
    private:
        Core::CQRS::Bridge* editorBridge = nullptr;
    };
}
