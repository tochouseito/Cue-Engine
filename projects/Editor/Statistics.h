#pragma once

// === Engine include ===
#include <frame/FrameController.h>

// === C++ include ===
#include <memory>
#include <cstdint>
#include <deque>

// === ImGui include ===
#include <imgui.h>

namespace Cue::Editor
{
    // 統計情報管理クラス
    class Statistics final
    {
    public:
        // フレームログの行構造体
        struct FrameLogLine final
        {
            uint64_t totalFrame = 0;
            float fps = 0.0f;
            uint32_t updateIndex = 0;
            uint32_t renderIndex = 0;
            uint32_t presentIndex = 0;
        };

        Statistics(FrameController& frameController)
            : frameController(frameController)
        {}
        ~Statistics() = default;

        void update()
        {
            const uint64_t totalFrame = frameController.total_frame();
            const float fps = static_cast<float>(frameController.frame_counter().fps());
            const uint32_t updateIndex = frameController.update_index();
            const uint32_t renderIndex = frameController.render_index();
            const uint32_t presentIndex = frameController.present_index();
            constexpr size_t kMaxFrameLogs = 120;
            if (frameLogs.empty() || frameLogs.back().totalFrame != totalFrame)
            {
                frameLogs.push_back(FrameLogLine{
                    totalFrame,
                    fps,
                    updateIndex,
                    renderIndex,
                    presentIndex });
                if (frameLogs.size() > kMaxFrameLogs)
                {
                    frameLogs.pop_front();
                }
            }

            ImGui::Begin("Frame Statistics"); // ウィンドウ開始

            ImGui::BeginChild("FrameLogConsole", ImVec2(0.0f, 140.0f), true);
            for (const FrameLogLine& line : frameLogs)
            {
                ImGui::Text(
                    "Frame: %llu, FPS: %.2f, UpdateIndex: %u, RenderIndex: %u, PresentIndex: %u",
                    static_cast<unsigned long long>(line.totalFrame),
                    line.fps,
                    line.updateIndex,
                    line.renderIndex,
                    line.presentIndex);
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
            {
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();

            if (ImGui::Button("Show FPS Details"))
            {
                showFpsDetails = !showFpsDetails;
                maxFps = fps;
                minFps = fps;
            }
            if (showFpsDetails)
            {
                if (fps > maxFps)
                {
                    maxFps = fps;
                }
                if (fps < minFps)
                {
                    minFps = fps;
                }
                ImGui::Text("Max FPS: %.1f", maxFps);
                ImGui::Text("Min FPS: %.1f", minFps);
            }

            ImGui::End();
        }

    private:
        FrameController& frameController;
        std::deque<FrameLogLine> frameLogs{};
        bool showFpsDetails = false;
        float maxFps = 0.0f;
        float minFps = 0.0f;
    };
}
