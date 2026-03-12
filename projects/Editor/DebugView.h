#pragma once

// === Platform include ===
#include <win_platform.h>

// === Engine include ===
#include <Engine.h>

// === C++ include ===
#include <memory>
#include <cstdint>
#include <deque>

// === ImGui include ===
#include <imgui.h>

namespace Cue::Editor
{
    // debug 画面クラス
    class DebugView final
    {
    public:
        DebugView(Cue::Platform::Win::WinPlatform& win, Cue::Engine& engine)
            : win(win), engine(engine) {}
        ~DebugView() = default;

        void update()
        {
            ImGui::Begin("Debug View"); // ウィンドウ開始

            Cue::CQRS::Queries::FinalColorPreviewQuery finalColorPreviewQuery{};
            Cue::CQRS::Queries::TexturePreviewQueryResult finalColorPreviewResult{};
            const Cue::Result getFinalColorDescriptorResult = engine.execute_editor_query(finalColorPreviewQuery, finalColorPreviewResult);
            if (getFinalColorDescriptorResult && finalColorPreviewResult.descriptorHandle.shaderVisible)
            {
                const float viewportWidth = static_cast<float>(win.window_width());
                const float viewportHeight = static_cast<float>(win.window_height());
                const float aspectRatio = (viewportHeight > 0.0f) ? (viewportWidth / viewportHeight) : 1.0f;
                ImVec2 imageSize = ImGui::GetContentRegionAvail();
                if (imageSize.x <= 0.0f)
                {
                    imageSize.x = 320.0f;
                }
                imageSize.y = imageSize.x / aspectRatio;
                if (imageSize.y > 320.0f)
                {
                    imageSize.y = 320.0f;
                    imageSize.x = imageSize.y * aspectRatio;
                }

                ImGui::Separator();
                ImGui::Text("FinalColor Preview");
                ImGui::Image(static_cast<ImTextureID>(finalColorPreviewResult.descriptorHandle.gpuPtr), imageSize);
            }
            else
            {
                ImGui::Separator();
                ImGui::Text("FinalColor Preview");
                ImGui::Text("FinalColor descriptor is not ready.");
            }

            ImGui::End();
        }
    private:
        Cue::Platform::Win::WinPlatform& win;
        Cue::Engine& engine;
    };
}
