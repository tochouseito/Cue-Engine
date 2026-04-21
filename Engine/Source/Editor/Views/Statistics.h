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
    class Statistics final
    {
    public:
        // フレームログの行構造体
        struct FrameLogLine final
        {
            uint64_t totalFrame = 0;
            float fps = 0.0f;
            double updateElapsedMs = 0.0;
            double renderElapsedMs = 0.0;
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
            const double updateElapsedMs = frameController.update_elapsed_ms();
            const double renderElapsedMs = frameController.render_elapsed_ms();
            const uint32_t updateIndex = frameController.update_index();
            const uint32_t renderIndex = frameController.render_index();
            const uint32_t presentIndex = frameController.present_index();
            constexpr size_t kMaxFrameLogs = 120;
            if (frameLogs.empty() || frameLogs.back().totalFrame != totalFrame)
            {
                frameLogs.push_back(FrameLogLine{
                    totalFrame,
                    fps,
                    updateElapsedMs,
                    renderElapsedMs,
                    updateIndex,
                    renderIndex,
                    presentIndex });
                if (frameLogs.size() > kMaxFrameLogs)
                {
                    frameLogs.pop_front();
                }
            }

            ImGui::Begin("Frame Statistics"); // ウィンドウ開始

            ImGui::Text("Update Thread: %.3f ms", updateElapsedMs);
            ImGui::Text("Render Thread: %.3f ms", renderElapsedMs);

            ImGui::BeginChild("FrameLogConsole", ImVec2(0.0f, 140.0f), true);
            for (const FrameLogLine& line : frameLogs)
            {
                ImGui::Text(
                    "Frame: %llu, FPS: %.2f, Update: %.3f ms, Render: %.3f ms, UpdateIndex: %u, RenderIndex: %u, PresentIndex: %u",
                    static_cast<unsigned long long>(line.totalFrame),
                    line.fps,
                    line.updateElapsedMs,
                    line.renderElapsedMs,
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
