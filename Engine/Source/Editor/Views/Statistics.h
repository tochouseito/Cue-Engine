#pragma once

// === Engine includes ===
#include <Engine.h>
#include <FrameController.h>

// === Editor includes ===
#include "EditorLoopMetrics.h"

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
            double renderFrameGraphMs = 0.0;
            double renderFrameGraphWorkMs = 0.0;
            double renderSubmitMs = 0.0;
            double renderQueueWaitMs = 0.0;
            double renderContextRecycleWaitMs = 0.0;
            double renderInterQueueWaitMs = 0.0;
            double renderFinalQueueWaitMs = 0.0;
            uint32_t updateIndex = 0;
            uint32_t renderIndex = 0;
            uint32_t presentIndex = 0;
        };

        Statistics(FrameController& frameController, Engine& engine)
            : frameController(frameController)
            , engine(engine)
        {}
        ~Statistics() = default;

        void set_loop_metrics_source(
            const EditorLoopMetrics* a_loopMetrics) noexcept
        {
            loopMetrics = a_loopMetrics;
        }

        void set_update_metrics_source(
            const EditorUpdateMetrics* a_updateMetrics) noexcept
        {
            updateMetrics = a_updateMetrics;
        }

        void update()
        {
            const uint64_t totalFrame = frameController.total_frame();
            const float fps = static_cast<float>(frameController.frame_counter().fps());
            const double updateElapsedMs = frameController.update_elapsed_ms();
            const double renderElapsedMs = frameController.render_elapsed_ms();
            const double stepElapsedMs = frameController.step_elapsed_ms();
            const double presentElapsedMs = frameController.present_elapsed_ms();
            const double counterTickElapsedMs =
                frameController.counter_tick_elapsed_ms();
            const uint32_t stepsPerPresent =
                frameController.steps_per_present();
            const RHI::FrameGraphExecutionStats renderStats =
                engine.render_frame_graph_summary_stats();
            const RHI::FrameGraphExecutionStats presentStats =
                engine.present_frame_graph_summary_stats();
            const double renderFrameGraphWorkMs = (std::max)(
                0.0,
                renderStats.totalExecuteMs -
                    renderStats.queueWaitMs -
                    renderStats.contextRecycleWaitMs);
            const uint32_t updateIndex = frameController.update_index();
            const uint32_t renderIndex = frameController.render_index();
            const uint32_t presentIndex = frameController.present_index();

            ImGui::Begin("Frame Statistics"); // ウィンドウ開始

            if (loopMetrics != nullptr)
            {
                ImGui::Text(
                    "Editor Loop Total: %.3f ms",
                    loopMetrics->loopTotalMs);
                ImGui::Text(
                    "  Poll: %.3f ms / ImGuiBegin: %.3f ms / ImGuiEnd: %.3f ms",
                    loopMetrics->pollMessageMs,
                    loopMetrics->imguiBeginMs,
                    loopMetrics->imguiEndMs);
                ImGui::Text(
                    "  EditorUpdate: %.3f ms / ProjectHub: %.3f ms / Draw: %s",
                    loopMetrics->editorUpdateMs,
                    loopMetrics->projectHubUpdateMs,
                    loopMetrics->didDrawImgui ? "true" : "false");
                ImGui::Text(
                    "  EngineBegin: %.3f ms / EngineTick: %.3f ms / EngineEnd: %.3f ms",
                    loopMetrics->engineBeginMs,
                    loopMetrics->engineTickMs,
                    loopMetrics->engineEndMs);
            }
            if (updateMetrics != nullptr)
            {
                ImGui::Text(
                    "EditorUpdate Total: %.3f ms",
                    updateMetrics->totalMs);
                ImGui::Text(
                    "  Pending: %.3f ms / Dockspace: %.3f ms / MenuBar: %.3f ms / Optional: %.3f ms",
                    updateMetrics->pendingScriptActionMs,
                    updateMetrics->dockspaceMs,
                    updateMetrics->menuBarMs,
                    updateMetrics->optionalWindowsMs);
                ImGui::Text(
                    "  Statistics: %.3f ms / DebugView: %.3f ms / AssetBrowser: %.3f ms",
                    updateMetrics->statisticsMs,
                    updateMetrics->debugViewMs,
                    updateMetrics->assetBrowserMs);
                ImGui::Text(
                    "  CreatePopup: %.3f ms / BuildNotice: %.3f ms / BuildOutput: %.3f ms",
                    updateMetrics->createScriptPopupMs,
                    updateMetrics->scriptBuildNotificationMs,
                    updateMetrics->scriptBuildOutputMs);
                ImGui::Text(
                    "  Hierarchy: %.3f ms / Inspector: %.3f ms",
                    updateMetrics->hierarchyMs,
                    updateMetrics->inspectorMs);
            }
            ImGui::Text("FrameController Step: %.3f ms", stepElapsedMs);
            ImGui::Text("Present Total: %.3f ms", presentElapsedMs);
            ImGui::Text("FrameCounter Tick: %.3f ms", counterTickElapsedMs);
            ImGui::Text("Steps / Present: %u", stepsPerPresent);
            ImGui::Text("Update Thread: %.3f ms", updateElapsedMs);
            ImGui::Text("Render Thread: %.3f ms", renderElapsedMs);
            ImGui::Text("FrameGraph Total: %.3f ms", renderStats.totalExecuteMs);
            ImGui::Text("FrameGraph Work: %.3f ms", renderFrameGraphWorkMs);
            ImGui::Text("Render Submit: %.3f ms", renderStats.submitMs);
            ImGui::Text("Render Queue Wait: %.3f ms", renderStats.queueWaitMs);
            ImGui::Text(
                "Context Recycle Wait: %.3f ms",
                renderStats.contextRecycleWaitMs);
            ImGui::Text(
                "  Inter-Queue: %.3f ms / Final: %.3f ms",
                renderStats.interQueueWaitMs,
                renderStats.finalQueueWaitMs);
            ImGui::Text(
                "  Final Gfx: %.3f ms / Compute: %.3f ms / Copy: %.3f ms",
                renderStats.finalGraphicsWaitMs,
                renderStats.finalComputeWaitMs,
                renderStats.finalCopyWaitMs);
            if (renderStats.hasGpuFrameMs)
            {
                ImGui::Text("GPU Frame: %.3f ms", renderStats.gpuFrameMs);
            }
            else
            {
                ImGui::TextUnformatted("GPU Frame: unavailable");
            }
            ImGui::Text(
                "Present FrameGraph: %.3f ms / Work %.3f ms / Wait %.3f ms / Recycle %.3f ms",
                presentStats.totalExecuteMs,
                (std::max)(0.0,
                    presentStats.totalExecuteMs -
                        presentStats.queueWaitMs -
                        presentStats.contextRecycleWaitMs),
                presentStats.queueWaitMs,
                presentStats.contextRecycleWaitMs);

            GameCore::GameWorld* displayWorld = engine.active_world();
            if (displayWorld == nullptr)
            {
                displayWorld = engine.editor_world();
            }
            if (displayWorld != nullptr)
            {
                bool isCpuBatchingEnabled =
                    displayWorld->is_cpu_batching_enabled();
                if (ImGui::Checkbox("CPU Batching", &isCpuBatchingEnabled))
                {
                    if (GameCore::GameWorld* editorWorld = engine.editor_world();
                        editorWorld != nullptr)
                    {
                        editorWorld->set_cpu_batching_enabled(
                            isCpuBatchingEnabled);
                    }

                    if (GameCore::GameWorld* playWorld = engine.play_world();
                        playWorld != nullptr)
                    {
                        playWorld->set_cpu_batching_enabled(
                            isCpuBatchingEnabled);
                    }
                }
            }

            if (ImGui::CollapsingHeader("Detailed Frame Stats"))
            {
                constexpr size_t kMaxFrameLogs = 32;
                if (frameLogs.empty() || frameLogs.back().totalFrame != totalFrame)
                {
                    frameLogs.push_back(FrameLogLine{
                        totalFrame,
                        fps,
                        updateElapsedMs,
                        renderElapsedMs,
                        renderStats.totalExecuteMs,
                        renderFrameGraphWorkMs,
                        renderStats.submitMs,
                        renderStats.queueWaitMs,
                        renderStats.contextRecycleWaitMs,
                        renderStats.interQueueWaitMs,
                        renderStats.finalQueueWaitMs,
                        updateIndex,
                        renderIndex,
                        presentIndex });
                    if (frameLogs.size() > kMaxFrameLogs)
                    {
                        frameLogs.pop_front();
                    }
                }

                ImGui::BeginChild("FrameLogConsole", ImVec2(0.0f, 100.0f), true);
                for (const FrameLogLine& line : frameLogs)
                {
                    ImGui::Text(
                        "Frame: %llu, FPS: %.2f, Update: %.3f ms, Render: %.3f ms, FG: %.3f ms, Work: %.3f ms, Submit: %.3f ms, Wait: %.3f ms, RecycleWait: %.3f ms, InterWait: %.3f ms, FinalWait: %.3f ms, UpdateIndex: %u, RenderIndex: %u, PresentIndex: %u",
                        static_cast<unsigned long long>(line.totalFrame),
                        line.fps,
                        line.updateElapsedMs,
                        line.renderElapsedMs,
                        line.renderFrameGraphMs,
                        line.renderFrameGraphWorkMs,
                        line.renderSubmitMs,
                        line.renderQueueWaitMs,
                        line.renderContextRecycleWaitMs,
                        line.renderInterQueueWaitMs,
                        line.renderFinalQueueWaitMs,
                        line.updateIndex,
                        line.renderIndex,
                        line.presentIndex);
                }
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
                {
                    ImGui::SetScrollHereY(1.0f);
                }
                ImGui::EndChild();

                const RHI::FrameGraphExecutionStats detailedRenderStats =
                    engine.render_frame_graph_stats();
                std::vector<RHI::FrameGraphExecutionStats::PassExecutionStats> passStats =
                    detailedRenderStats.passStats;
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
                    ImGui::Text(
                        "  submit_exec %.3f / signal %.3f / cmdlists %u",
                        passStat.submitExecuteListsMs,
                        passStat.submitSignalOnlyMs,
                        passStat.submittedCommandListCount);
                    if (passStat.hasGpuExecuteMs)
                    {
                        ImGui::Text("  gpu_exec %.3f", passStat.gpuExecuteMs);
                    }
                    for (const auto& detailTiming : passStat.detailTimings)
                    {
                        ImGui::Text(
                            "  %s %.3f",
                            detailTiming.label.c_str(),
                            detailTiming.elapsedMs);
                    }
                }
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
        Engine& engine;
        const EditorLoopMetrics* loopMetrics = nullptr;
        const EditorUpdateMetrics* updateMetrics = nullptr;
        std::deque<FrameLogLine> frameLogs{};
        bool showFpsDetails = false;
        float maxFps = 0.0f;
        float minFps = 0.0f;
    };
}
