#pragma once

// === Engine includes ===
#include <Engine.h>
#include <FrameController.h>

// === C++ includes ===
#include <algorithm>
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
            double renderSubmitMs = 0.0;
            double renderQueueWaitMs = 0.0;
            uint32_t updateIndex = 0;
            uint32_t renderIndex = 0;
            uint32_t presentIndex = 0;
        };

        Statistics(FrameController& frameController, const Engine& engine)
            : frameController(frameController)
            , engine(engine)
        {}
        ~Statistics() = default;

        void update()
        {
            const uint64_t totalFrame = frameController.total_frame();
            const float fps = static_cast<float>(frameController.frame_counter().fps());
            const double updateElapsedMs = frameController.update_elapsed_ms();
            const double renderElapsedMs = frameController.render_elapsed_ms();
            const RHI::FrameGraphExecutionStats& renderStats =
                engine.render_frame_graph_stats();
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
                    renderStats.submitMs,
                    renderStats.queueWaitMs,
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
            ImGui::Text("Render Submit: %.3f ms", renderStats.submitMs);
            ImGui::Text("Render Queue Wait: %.3f ms", renderStats.queueWaitMs);
            if (renderStats.hasGpuFrameMs)
            {
                ImGui::Text("GPU Frame: %.3f ms", renderStats.gpuFrameMs);
            }
            else
            {
                ImGui::TextUnformatted("GPU Frame: unavailable");
            }

            ImGui::BeginChild("FrameLogConsole", ImVec2(0.0f, 140.0f), true);
            for (const FrameLogLine& line : frameLogs)
            {
                ImGui::Text(
                    "Frame: %llu, FPS: %.2f, Update: %.3f ms, Render: %.3f ms, Submit: %.3f ms, Wait: %.3f ms, UpdateIndex: %u, RenderIndex: %u, PresentIndex: %u",
                    static_cast<unsigned long long>(line.totalFrame),
                    line.fps,
                    line.updateElapsedMs,
                    line.renderElapsedMs,
                    line.renderSubmitMs,
                    line.renderQueueWaitMs,
                    line.updateIndex,
                    line.renderIndex,
                    line.presentIndex);
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
            {
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();

            std::vector<RHI::FrameGraphExecutionStats::PassExecutionStats> passStats =
                renderStats.passStats;
            std::sort(passStats.begin(), passStats.end(),
                [](const auto& a_left, const auto& a_right)
                {
                    const double leftTotal =
                        a_left.acquireResetSetupMs +
                        a_left.preBarrierMs +
                        a_left.cpuExecuteMs +
                        a_left.postBarrierMs +
                        a_left.closeMs +
                        a_left.submitSignalMs;
                    const double rightTotal =
                        a_right.acquireResetSetupMs +
                        a_right.preBarrierMs +
                        a_right.cpuExecuteMs +
                        a_right.postBarrierMs +
                        a_right.closeMs +
                        a_right.submitSignalMs;
                    return leftTotal > rightTotal;
                });

            ImGui::Separator();
            ImGui::TextUnformatted("Render Pass CPU");
            const size_t passCount =
                (std::min)(passStats.size(), static_cast<size_t>(8));
            for (size_t i = 0; i < passCount; ++i)
            {
                const auto& passStat = passStats[i];
                const char* queueName = "Graphics";
                switch (passStat.queueType)
                {
                case RHI::CommandListType::Compute:
                    queueName = "Compute";
                    break;
                case RHI::CommandListType::Copy:
                    queueName = "Copy";
                    break;
                case RHI::CommandListType::Graphics:
                default:
                    break;
                }

                ImGui::Text(
                    "%s [%s]: %.3f ms",
                    passStat.name.data(),
                    queueName,
                    passStat.acquireResetSetupMs +
                    passStat.preBarrierMs +
                    passStat.cpuExecuteMs +
                    passStat.postBarrierMs +
                    passStat.closeMs +
                    passStat.submitSignalMs);
                ImGui::Text(
                    "  setup %.3f / pre %.3f / exec %.3f / post %.3f / close %.3f / submit %.3f",
                    passStat.acquireResetSetupMs,
                    passStat.preBarrierMs,
                    passStat.cpuExecuteMs,
                    passStat.postBarrierMs,
                    passStat.closeMs,
                    passStat.submitSignalMs);
            }

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
        const Engine& engine;
        std::deque<FrameLogLine> frameLogs{};
        bool showFpsDetails = false;
        float maxFps = 0.0f;
        float minFps = 0.0f;
    };
}
