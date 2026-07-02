// === Base includes ===
#include <CueAssert.h>
#include <CueResult.h>

// === Core includes ===
#include <CQRS/CQRS.h>
#include <DebugTool/PerformanceCounter.h>
#include <IO/Logger.h>
#include <Time/FrameCounter.h>

// === WinPlatform includes ===
#include <win_platform.h>

// === D3D12Backend includes ===
#include <D3D12Backend.h>

// === Editor includes ===
#include "DebugCamera.h"
#include <Asset/ModelImporter.h>

// === Engine includes ===
#include <Engine.h>

// === ImGui includes ===
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

// === C++ includes ===
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace Cue;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd,
                                                             UINT message,
                                                             WPARAM wParam,
                                                             LPARAM lParam);

namespace {
bool g_observerViewEnabled = false;
bool g_controlObserverCamera = false;
static constexpr uint64_t kPassTimingWarmupSamples = 120;

[[nodiscard]] bool is_key_down(int virtualKey) noexcept {
  return (::GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

[[nodiscard]] bool was_key_pressed(int virtualKey) noexcept {
  return (::GetAsyncKeyState(virtualKey) & 0x0001) != 0;
}

[[nodiscard]] ImFont *add_font_if_exists(ImFontAtlas &fonts,
                                         std::string_view path,
                                         float sizePixels,
                                         ImFontConfig &fontConfig) {
  if (!std::filesystem::exists(std::filesystem::path(path))) {
    return nullptr;
  }
  return fonts.AddFontFromFileTTF(std::string(path).c_str(), sizePixels,
                                  &fontConfig);
}

[[nodiscard]] Editor::DebugCamera::Input
make_debug_camera_input(HWND windowHandle, float deltaSeconds) noexcept {
  Editor::DebugCamera::Input input{};
  input.deltaSeconds = deltaSeconds;

  if (windowHandle == nullptr || ::GetForegroundWindow() != windowHandle) {
    return input;
  }

  input.moveForward = is_key_down('W');
  input.moveBackward = is_key_down('S');
  input.moveLeft = is_key_down('A');
  input.moveRight = is_key_down('D');
  input.moveUp = is_key_down(VK_SPACE);
  input.moveDown = is_key_down(VK_CONTROL);
  input.fast = is_key_down(VK_SHIFT);
  input.lookActive = is_key_down(VK_RBUTTON);

  static bool hadPreviousMousePosition = false;
  static POINT previousMousePosition{};

  POINT currentMousePosition{};
  if (!::GetCursorPos(&currentMousePosition)) {
    hadPreviousMousePosition = false;
    return input;
  }

  if (input.lookActive) {
    if (!hadPreviousMousePosition) {
      previousMousePosition = currentMousePosition;
      hadPreviousMousePosition = true;
      ::SetCapture(windowHandle);
    } else {
      input.mouseDeltaX =
          static_cast<float>(currentMousePosition.x - previousMousePosition.x);
      input.mouseDeltaY =
          static_cast<float>(currentMousePosition.y - previousMousePosition.y);
      previousMousePosition = currentMousePosition;
    }
  } else {
    if (hadPreviousMousePosition) {
      ::ReleaseCapture();
    }
    hadPreviousMousePosition = false;
  }

  return input;
}

[[nodiscard]] double
pass_gpu_ms(const RHI::FrameGraphExecutionStats &stats,
            std::initializer_list<std::string_view> passNames) noexcept {
  double total = 0.0;
  for (const RHI::FrameGraphExecutionStats::PassExecutionStats &pass :
       stats.passStats) {
    if (!pass.hasGpuExecuteMs) {
      continue;
    }
    for (std::string_view passName : passNames) {
      if (pass.name == passName) {
        total += pass.gpuExecuteMs;
        break;
      }
    }
  }
  return total;
}

struct CpuFrameTiming final {
  double totalMs = 0.0;
  double setupMs = 0.0;
  double pollMessageMs = 0.0;
  double platformBeginMs = 0.0;
  double engineBeginMs = 0.0;
  double inputCameraMs = 0.0;
  double engineTickMs = 0.0;
  double engineEndMs = 0.0;
  double platformEndMs = 0.0;
};

[[nodiscard]] const char *
command_queue_name(RHI::CommandListType type) noexcept;

struct TimingAccumulator final {
  uint64_t samples = 0;
  double sumMs = 0.0;
  double minMs = std::numeric_limits<double>::max();
  double maxMs = 0.0;
  std::vector<double> values{};

  void add(double valueMs) {
    ++samples;
    sumMs += valueMs;
    minMs = (std::min)(minMs, valueMs);
    maxMs = (std::max)(maxMs, valueMs);
    values.push_back(valueMs);
  }

  [[nodiscard]] double average_ms() const noexcept {
    return samples > 0 ? sumMs / static_cast<double>(samples) : 0.0;
  }

  [[nodiscard]] double min_or_zero_ms() const noexcept {
    return samples > 0 ? minMs : 0.0;
  }
};

struct TimingSummaryStats final {
  uint64_t samples = 0;
  uint64_t warmupSkipped = 0;
  double average = 0.0;
  double min = 0.0;
  double max = 0.0;
  double p50 = 0.0;
  double p95 = 0.0;
  double p99 = 0.0;
};

[[nodiscard]] double percentile_from_sorted(const std::vector<double> &values,
                                            double percentile) noexcept {
  if (values.empty()) {
    return 0.0;
  }
  const size_t lastIndex = values.size() - 1u;
  const double scaledIndex = percentile * static_cast<double>(lastIndex);
  const size_t index = (std::min)(
      lastIndex, static_cast<size_t>(scaledIndex + 0.5));
  return values[index];
}

[[nodiscard]] TimingSummaryStats
summarize_values_after_warmup(const TimingAccumulator &accumulator,
                              uint64_t warmupSamples) {
  TimingSummaryStats result{};
  if (accumulator.values.empty()) {
    return result;
  }

  const size_t sampleCount = accumulator.values.size();
  const size_t beginIndex =
      sampleCount > warmupSamples ? static_cast<size_t>(warmupSamples) : 0u;
  result.warmupSkipped = static_cast<uint64_t>(beginIndex);

  std::vector<double> values(accumulator.values.begin() + beginIndex,
                             accumulator.values.end());
  std::sort(values.begin(), values.end());
  result.samples = static_cast<uint64_t>(values.size());
  result.min = values.front();
  result.max = values.back();
  double sum = 0.0;
  for (double value : values) {
    sum += value;
  }
  result.average = sum / static_cast<double>(values.size());
  result.p50 = percentile_from_sorted(values, 0.50);
  result.p95 = percentile_from_sorted(values, 0.95);
  result.p99 = percentile_from_sorted(values, 0.99);
  return result;
}

struct PassTimingAggregate final {
  std::string graphName{};
  std::string name{};
  RHI::CommandListType queueType = RHI::CommandListType::Graphics;
  uint32_t queueLane = 0;
  TimingAccumulator cpuExecute{};
  TimingAccumulator cpuRecordTotal{};
  TimingAccumulator gpuExecute{};
  uint64_t submittedCommandLists = 0;
};

class PassTimingCollector final {
public:
  void add_frame(std::string_view graphName,
                 const RHI::FrameGraphExecutionStats &stats) {
    for (const RHI::FrameGraphExecutionStats::PassExecutionStats &pass :
         stats.passStats) {
      PassTimingAggregate &aggregate = find_or_create(
          std::string(graphName), std::string(pass.name), pass.queueType,
          pass.queueLane);
      aggregate.queueType = pass.queueType;
      aggregate.queueLane = pass.queueLane;
      aggregate.cpuExecute.add(pass.cpuExecuteMs);
      aggregate.cpuRecordTotal.add(
          pass.acquireResetSetupMs + pass.preBarrierMs + pass.cpuExecuteMs +
          pass.postBarrierMs + pass.closeMs + pass.submitExecuteListsMs +
          pass.submitSignalOnlyMs + pass.submitSignalMs);
      if (pass.hasGpuExecuteMs) {
        aggregate.gpuExecute.add(pass.gpuExecuteMs);
      }
      aggregate.submittedCommandLists += pass.submittedCommandListCount;
    }
  }

  void log_summary() const {
    Core::IO::log(Core::IO::LogSink::file,
                  "[PassTimingSummaryBegin] passCount={} warmupSamples={}",
                  m_passAggregates.size(), kPassTimingWarmupSamples);
    for (const PassTimingAggregate &pass : m_passAggregates) {
      const TimingSummaryStats cpuExecute =
          summarize_values_after_warmup(pass.cpuExecute,
                                        kPassTimingWarmupSamples);
      const TimingSummaryStats cpuRecordTotal =
          summarize_values_after_warmup(pass.cpuRecordTotal,
                                        kPassTimingWarmupSamples);
      const TimingSummaryStats gpuExecute =
          summarize_values_after_warmup(pass.gpuExecute,
                                        kPassTimingWarmupSamples);
      Core::IO::log(
          Core::IO::LogSink::file,
          "[PassTimingSummary] graph={} pass={} queue={} lane={} cpuSamples={} "
          "cpuWarmupSkipped={} cpuExecuteMs(avg={:.3f} min={:.3f} "
          "max={:.3f} p50={:.3f} p95={:.3f} p99={:.3f}) "
          "cpuRecordTotalMs(avg={:.3f} min={:.3f} max={:.3f} p50={:.3f} "
          "p95={:.3f} p99={:.3f}) gpuSamples={} gpuWarmupSkipped={} "
          "gpuExecuteMs(avg={:.3f} min={:.3f} max={:.3f} p50={:.3f} "
          "p95={:.3f} p99={:.3f}) "
          "submittedCommandLists={}",
          pass.graphName, pass.name, command_queue_name(pass.queueType),
          pass.queueLane, pass.cpuExecute.samples, cpuExecute.warmupSkipped,
          cpuExecute.average, cpuExecute.min, cpuExecute.max, cpuExecute.p50,
          cpuExecute.p95, cpuExecute.p99, cpuRecordTotal.average,
          cpuRecordTotal.min, cpuRecordTotal.max, cpuRecordTotal.p50,
          cpuRecordTotal.p95, cpuRecordTotal.p99, pass.gpuExecute.samples,
          gpuExecute.warmupSkipped, gpuExecute.average, gpuExecute.min,
          gpuExecute.max, gpuExecute.p50, gpuExecute.p95, gpuExecute.p99,
          pass.submittedCommandLists);
    }
    Core::IO::log(Core::IO::LogSink::file, "[PassTimingSummaryEnd]");
  }

private:
  PassTimingAggregate &find_or_create(std::string graphName, std::string name,
                                      RHI::CommandListType queueType,
                                      uint32_t queueLane) {
    for (PassTimingAggregate &aggregate : m_passAggregates) {
      if (aggregate.graphName == graphName && aggregate.name == name &&
          aggregate.queueType == queueType && aggregate.queueLane == queueLane) {
        return aggregate;
      }
    }

    PassTimingAggregate aggregate{};
    aggregate.graphName = std::move(graphName);
    aggregate.name = std::move(name);
    aggregate.queueType = queueType;
    aggregate.queueLane = queueLane;
    m_passAggregates.push_back(std::move(aggregate));
    return m_passAggregates.back();
  }

  std::vector<PassTimingAggregate> m_passAggregates{};
};

struct RenderWorkloadMetric final {
  std::string name{};
  TimingAccumulator values{};
};

class RenderWorkloadCollector final {
public:
  void add_frame(const EngineDebugStats &debugStats) {
    const GpuData::MeshletChunkVisibilityStatsGpu &chunkStats =
        debugStats.meshletChunkVisibilityStats;
    const GpuData::MeshletGroupCullStatsGpu &groupStats =
        debugStats.meshletGroupCullStats;
    const GpuData::StaticMeshBatchStatsGpu &batchStats =
        debugStats.staticMeshBatchStats;
    const GpuData::ObjectCullLodStatsGpu &lodStats =
        debugStats.objectCullLodStats;

    add_metric("objects.total", debugStats.totalObjects);
    add_metric("objects.cpuVisible", debugStats.visibleObjects);
    add_metric("objects.gpuVisible", lodStats.selectedObjectCount);
    add_metric("objects.gpuCulled",
               debugStats.totalObjects > lodStats.selectedObjectCount
                   ? debugStats.totalObjects - lodStats.selectedObjectCount
                   : 0u);
    add_metric("objects.selectedForChunkDepth",
               chunkStats.selectedObjectCount);
    add_metric("chunks.depthCommands", chunkStats.commandCount);
    add_metric("chunks.depthInstances", chunkStats.instanceCount);
    add_metric("chunks.currentVisible", chunkStats.currentVisibleChunkCount);
    add_metric("chunks.previousVisible", chunkStats.previousVisibleChunkCount);
    add_metric("chunks.testedObjectChunks", chunkStats.testedObjectChunkCount);
    add_metric("chunks.rejectedByPreviousVisibility",
               chunkStats.rejectedByPreviousVisibility);
    add_metric("chunks.rejectedByFrustum", chunkStats.rejectedByFrustum);
    add_metric("chunks.rejectedByOccluder",
               chunkStats.rejectedByOccluderFilter);
    add_metric("occlusion.enabled", chunkStats.occlusionEnabled);
    add_metric("occlusion.tested", chunkStats.occlusionTestedCount);
    add_metric("occlusion.rejected", chunkStats.occlusionRejectedCount);
    add_metric("chunks.commandOverflow", chunkStats.commandOverflowCount);
    add_metric("staticBatch.commandCount", batchStats.commandCount);
    add_metric("staticBatch.instanceCount", batchStats.instanceCount);
    add_metric("staticBatch.submittedIndexCount",
               batchStats.submittedIndexCount);
    add_metric("staticBatch.overflowCount", batchStats.overflowCount);
    add_metric("visibility.submittedIndexCount",
               static_cast<uint64_t>(batchStats.submittedIndexCount) +
                   static_cast<uint64_t>(groupStats.rangeIndexCount));
    add_metric("objectCullLod.selectedObjects", lodStats.selectedObjectCount);
    add_metric("objectCullLod.lod0", lodStats.lodObjectCounts[0]);
    add_metric("objectCullLod.lod1", lodStats.lodObjectCounts[1]);
    add_metric("objectCullLod.lod2", lodStats.lodObjectCounts[2]);
    add_metric("objectCullLod.lod3", lodStats.lodObjectCounts[3]);
    add_metric("objectCullLod.lod4", lodStats.lodObjectCounts[4]);
    add_metric("objectCullLod.impostor", lodStats.impostorCount);
    add_metric("meshletGroup.candidateObjects",
               groupStats.candidateObjectCount);
    add_metric("meshletGroup.testedGroups", groupStats.testedGroupCount);
    add_metric("meshletGroup.visibleGroups", groupStats.visibleGroupCount);
    add_metric("meshletGroup.fallbackObjects",
               groupStats.fallbackObjectCount);
    add_metric("meshletGroup.rangeObjects", groupStats.rangeObjectCount);
    add_metric("meshletGroup.rangeCommands", groupStats.rangeCommandCount);
    add_metric("meshletGroup.rangeIndices", groupStats.rangeIndexCount);
    add_metric("meshletGroup.totalIndices", groupStats.totalIndexCount);
    add_metric("meshletGroup.occlusionTested",
               groupStats.occlusionTestedCount);
    add_metric("meshletGroup.occlusionRejected",
               groupStats.occlusionRejectedCount);
    add_metric("lod.lod0", debugStats.lodObjectCounts[0]);
    add_metric("lod.lod1", debugStats.lodObjectCounts[1]);
    add_metric("lod.lod2", debugStats.lodObjectCounts[2]);
    add_metric("lod.lod3", debugStats.lodObjectCounts[3]);
    add_metric("lod.lod4", debugStats.lodObjectCounts[4]);
    add_metric("lod.impostor", debugStats.impostorCount);
  }

  void log_summary() const {
    Core::IO::log(Core::IO::LogSink::file,
                  "[RenderWorkloadSummaryBegin] metricCount={} "
                  "warmupSamples={}",
                  m_metrics.size(), kPassTimingWarmupSamples);
    for (const RenderWorkloadMetric &metric : m_metrics) {
      const TimingSummaryStats stats =
          summarize_values_after_warmup(metric.values,
                                        kPassTimingWarmupSamples);
      Core::IO::log(
          Core::IO::LogSink::file,
          "[RenderWorkloadSummary] metric={} samples={} warmupSkipped={} "
          "value(avg={:.3f} min={:.3f} max={:.3f} p50={:.3f} p95={:.3f} "
          "p99={:.3f})",
          metric.name, metric.values.samples, stats.warmupSkipped,
          stats.average, stats.min, stats.max, stats.p50, stats.p95,
          stats.p99);
    }
    Core::IO::log(Core::IO::LogSink::file, "[RenderWorkloadSummaryEnd]");
  }

private:
  void add_metric(std::string_view name, uint64_t value) {
    RenderWorkloadMetric &metric = find_or_create(name);
    metric.values.add(static_cast<double>(value));
  }

  RenderWorkloadMetric &find_or_create(std::string_view name) {
    for (RenderWorkloadMetric &metric : m_metrics) {
      if (metric.name == name) {
        return metric;
      }
    }

    RenderWorkloadMetric metric{};
    metric.name = std::string(name);
    m_metrics.push_back(std::move(metric));
    return m_metrics.back();
  }

  std::vector<RenderWorkloadMetric> m_metrics{};
};

using TimingClock = std::chrono::steady_clock;
using TimingPoint = TimingClock::time_point;

[[nodiscard]] double elapsed_ms(TimingPoint begin, TimingPoint end) noexcept {
  return std::chrono::duration<double, std::milli>(end - begin).count();
}

[[nodiscard]] const char *
command_queue_name(RHI::CommandListType type) noexcept {
  switch (type) {
  case RHI::CommandListType::Graphics:
    return "Graphics";
  case RHI::CommandListType::Compute:
    return "Compute";
  case RHI::CommandListType::Copy:
    return "Copy";
  default:
    return "Unknown";
  }
}

[[nodiscard]] DrawSystem::RenderDebugView
next_render_debug_view(DrawSystem::RenderDebugView view) noexcept {
  switch (view) {
  case DrawSystem::RenderDebugView::Forward:
    return DrawSystem::RenderDebugView::VisibilityObjectId;
  case DrawSystem::RenderDebugView::VisibilityObjectId:
    return DrawSystem::RenderDebugView::VisibilityPrimitiveId;
  case DrawSystem::RenderDebugView::VisibilityPrimitiveId:
    return DrawSystem::RenderDebugView::VisibilityBarycentric;
  case DrawSystem::RenderDebugView::VisibilityBarycentric:
    return DrawSystem::RenderDebugView::VisibilityNormal;
  case DrawSystem::RenderDebugView::VisibilityNormal:
    return DrawSystem::RenderDebugView::VisibilityUv;
  case DrawSystem::RenderDebugView::VisibilityUv:
    return DrawSystem::RenderDebugView::VisibilityLit;
  case DrawSystem::RenderDebugView::VisibilityLit:
    return DrawSystem::RenderDebugView::VisibilityMaterial;
  case DrawSystem::RenderDebugView::VisibilityMaterial:
    return DrawSystem::RenderDebugView::RenderPath;
  case DrawSystem::RenderDebugView::RenderPath:
  default:
    return DrawSystem::RenderDebugView::Forward;
  }
}

[[nodiscard]] const char *
render_debug_view_name(DrawSystem::RenderDebugView view) noexcept {
  switch (view) {
  case DrawSystem::RenderDebugView::Forward:
    return "Forward";
  case DrawSystem::RenderDebugView::VisibilityObjectId:
    return "Visibility ObjectId";
  case DrawSystem::RenderDebugView::VisibilityPrimitiveId:
    return "Visibility PrimitiveId";
  case DrawSystem::RenderDebugView::VisibilityBarycentric:
    return "Visibility Barycentric";
  case DrawSystem::RenderDebugView::VisibilityNormal:
    return "Visibility Normal";
  case DrawSystem::RenderDebugView::VisibilityUv:
    return "Visibility UV";
  case DrawSystem::RenderDebugView::VisibilityLit:
    return "Visibility Lit";
  case DrawSystem::RenderDebugView::VisibilityMaterial:
    return "Visibility Material";
  case DrawSystem::RenderDebugView::RenderPath:
    return "Render Path";
  default:
    return "Forward";
  }
}

[[nodiscard]] const char *
render_path_name(DrawSystem::RenderPath path) noexcept {
  switch (path) {
  case DrawSystem::RenderPath::Forward:
    return "Forward";
  case DrawSystem::RenderPath::VisibilityBuffer:
    return "Visibility Buffer";
  default:
    return "Forward";
  }
}

void log_render_debug_stats(uint64_t frameIndex, double fps,
                            const EngineDebugStats &debugStats,
                            const RHI::FrameGraphExecutionStats &frameStats) {
  const GpuData::MeshletChunkVisibilityStatsGpu &chunkStats =
      debugStats.meshletChunkVisibilityStats;
  const GpuData::MeshletGroupCullStatsGpu &groupStats =
      debugStats.meshletGroupCullStats;
  Core::IO::log(
      Core::IO::LogSink::file,
      "[RenderStats] frame={} fps={:.2f} objects(total={} visible={} culled={} "
      "cpuVisible={} cpuFrustumCulled={}) chunks(capacity={} allocated={} "
      "commands={} instances={} currentVisible={} previousVisible={} tested={} "
      "selectedObjects={} rejectObject={} skipObjectChunks={} "
      "skipMaxChunks={} rejectPrev={} rejectFrustum={} rejectOccluder={} "
      "overflow={} settings(maxChunksPerObject={} minObjectPx={} "
      "minChunkPx={} maxDepth={} maxDepthBin={}) occlusion(enabled={} "
      "tested={} rejected={})) meshletGroup(settings={} visibleObjects={} "
      "candidates={} testedGroups={} rejectFrustumGroups={} "
      "coneTestedMeshlets={} coneRejectedMeshlets={} visibleGroups={} "
      "culledObjects={} fallbackObjects={} rangeObjects={} rangeCommands={} "
      "rangeIndices={} totalIndices={}) gpuMs(objectCull={:.3f} "
      "meshletGroupCull={:.3f} buildChunkDepth={:.3f} chunkDepthDraw={:.3f} "
      "chunkHiZBuild={:.3f} batching={:.3f} forward={:.3f})",
      frameIndex, fps, chunkStats.totalObjectCount,
      chunkStats.visibleObjectCount, chunkStats.culledObjectCount,
      debugStats.visibleObjects, debugStats.frustumCulledObjects,
      debugStats.meshletChunkCapacity, debugStats.meshletChunkCount,
      chunkStats.commandCount, chunkStats.instanceCount,
      chunkStats.currentVisibleChunkCount, chunkStats.previousVisibleChunkCount,
      chunkStats.testedObjectChunkCount, chunkStats.selectedObjectCount,
      chunkStats.rejectedByObjectFilter, chunkStats.skippedChunksByObjectFilter,
      chunkStats.skippedChunksByMaxChunks,
      chunkStats.rejectedByPreviousVisibility, chunkStats.rejectedByFrustum,
      chunkStats.rejectedByOccluderFilter, chunkStats.commandOverflowCount,
      chunkStats.maxChunksPerObject, chunkStats.minObjectScreenRadiusPx,
      chunkStats.minChunkScreenRadiusPx, chunkStats.maxViewDepth,
      chunkStats.maxDepthBin, chunkStats.occlusionEnabled,
      chunkStats.occlusionTestedCount, chunkStats.occlusionRejectedCount,
      groupStats.settings, groupStats.visibleObjectCount,
      groupStats.candidateObjectCount, groupStats.testedGroupCount,
      groupStats.frustumRejectedGroupCount, groupStats.coneTestedMeshletCount,
      groupStats.coneRejectedMeshletCount, groupStats.visibleGroupCount,
      groupStats.culledObjectCount, groupStats.fallbackObjectCount,
      groupStats.rangeObjectCount, groupStats.rangeCommandCount,
      groupStats.rangeIndexCount, groupStats.totalIndexCount,
      pass_gpu_ms(frameStats, {"ObjectCullAndLod"}),
      pass_gpu_ms(frameStats, {"MeshletGroupCull"}),
      pass_gpu_ms(frameStats, {"BuildChunkDepthCommands"}),
      pass_gpu_ms(frameStats, {"ChunkDepthOnlyDraw"}),
      pass_gpu_ms(frameStats, {"ChunkHiZBuild"}),
      pass_gpu_ms(frameStats, {"BatchCount", "PrefixSum", "BatchFill"}),
      pass_gpu_ms(frameStats, {"StaticMeshForward"}));
}

void log_frame_timing(uint64_t frameIndex, double fps,
                      const CpuFrameTiming &cpuTiming,
                      const RHI::FrameGraphExecutionStats &frameStats) {
  Core::IO::log(
      Core::IO::LogSink::file,
      "[FrameTiming] frame={} fps={:.2f} cpuMs(total={:.3f} setup={:.3f} "
      "pollMessage={:.3f} platformBegin={:.3f} engineBegin={:.3f} "
      "inputCamera={:.3f} engineTick={:.3f} engineEnd={:.3f} "
      "platformEnd={:.3f}) frameGraphMs(totalExecute={:.3f} submit={:.3f} "
      "queueWait={:.3f} interQueueWait={:.3f} finalQueueWait={:.3f} "
      "contextRecycleWait={:.3f} finalGraphicsWait={:.3f} "
      "finalComputeWait={:.3f} finalCopyWait={:.3f} gpuFrameValid={} "
      "gpuFrame={:.3f})",
      frameIndex, fps, cpuTiming.totalMs, cpuTiming.setupMs,
      cpuTiming.pollMessageMs, cpuTiming.platformBeginMs,
      cpuTiming.engineBeginMs, cpuTiming.inputCameraMs, cpuTiming.engineTickMs,
      cpuTiming.engineEndMs, cpuTiming.platformEndMs, frameStats.totalExecuteMs,
      frameStats.submitMs, frameStats.queueWaitMs, frameStats.interQueueWaitMs,
      frameStats.finalQueueWaitMs, frameStats.contextRecycleWaitMs,
      frameStats.finalGraphicsWaitMs, frameStats.finalComputeWaitMs,
      frameStats.finalCopyWaitMs, frameStats.hasGpuFrameMs,
      frameStats.hasGpuFrameMs ? frameStats.gpuFrameMs
                               : frameStats.totalExecuteMs);

  for (const RHI::FrameGraphExecutionStats::PassExecutionStats &pass :
       frameStats.passStats) {
    Core::IO::log(
        Core::IO::LogSink::file,
        "[GpuPassTiming] frame={} pass={} queue={} lane={} gpuValid={} "
        "gpuMs={:.3f} "
        "cpuExecuteMs={:.3f} acquireResetSetupMs={:.3f} preBarrierMs={:.3f} "
        "postBarrierMs={:.3f} closeMs={:.3f} submitExecuteListsMs={:.3f} "
        "submitSignalOnlyMs={:.3f} submitSignalMs={:.3f} commandLists={}",
        frameIndex, pass.name, command_queue_name(pass.queueType),
        pass.queueLane, pass.hasGpuExecuteMs,
        pass.hasGpuExecuteMs ? pass.gpuExecuteMs : 0.0, pass.cpuExecuteMs,
        pass.acquireResetSetupMs, pass.preBarrierMs, pass.postBarrierMs,
        pass.closeMs, pass.submitExecuteListsMs, pass.submitSignalOnlyMs,
        pass.submitSignalMs,
        pass.submittedCommandListCount);

    for (const RHI::FrameGraphExecutionStats::PassExecutionStats::DetailTiming
             &detail : pass.detailTimings) {
      Core::IO::log(Core::IO::LogSink::file,
                    "[GpuPassTimingDetail] frame={} pass={} label={} "
                    "elapsedMs={:.3f}",
                    frameIndex, pass.name, detail.label, detail.elapsedMs);
    }
  }
}

class ImGuiOverlayPass final : public RHI::FrameGraphPass {
public:
  ImGuiOverlayPass(HWND hwnd, RHI::DX12::D3D12Backend &backend, Engine &engine)
      : m_backend(backend), m_engine(engine) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = "config/editor/imgui.ini";

    ImFontConfig fontConfig{};
    fontConfig.OversampleH = 3;
    fontConfig.OversampleV = 2;
    fontConfig.RasterizerMultiply = 1.45f;
    ImFont *editorFont = nullptr;
    for (std::string_view path :
         {"Engine/Fonts/NotoSansJP-VariableFont_wght.ttf",
          "../../../../Engine/Fonts/NotoSansJP-VariableFont_wght.ttf",
          "EngineResources/Fonts/NotoSansJP-VariableFont_wght.ttf"}) {
      editorFont = add_font_if_exists(*io.Fonts, path, 18.0f, fontConfig);
      if (editorFont != nullptr) {
        break;
      }
    }
    if (editorFont == nullptr) {
      for (std::string_view path :
           {"Engine/Fonts/Inter-VariableFont_opsz,wght.ttf",
            "../../../../Engine/Fonts/Inter-VariableFont_opsz,wght.ttf",
            "EngineResources/Fonts/Inter-VariableFont_opsz,wght.ttf"}) {
        editorFont = add_font_if_exists(*io.Fonts, path, 18.0f, fontConfig);
        if (editorFont != nullptr) {
          break;
        }
      }
    }
    if (editorFont == nullptr) {
      io.Fonts->AddFontDefault();
    }
    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Text] = ImVec4(0.98f, 0.98f, 0.98f, 1.0f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.72f, 0.72f, 0.72f, 1.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.05f, 0.06f, 0.94f);

    ImGui_ImplWin32_Init(hwnd);

    ImGui_ImplDX12_InitInfo initInfo{};
    initInfo.Device = backend.imgui_device();
    initInfo.CommandQueue = backend.imgui_command_queue();
    initInfo.NumFramesInFlight = 3;
    initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
    initInfo.UserData = &backend;
    initInfo.SrvDescriptorHeap = backend.imgui_srv_descriptor_heap();
    initInfo.SrvDescriptorAllocFn = &allocate_srv_descriptor;
    initInfo.SrvDescriptorFreeFn = &free_srv_descriptor;
    m_initialized = ImGui_ImplDX12_Init(&initInfo);
  }

  ~ImGuiOverlayPass() override {
    if (m_initialized) {
      ImGui_ImplDX12_Shutdown();
      ImGui_ImplWin32_Shutdown();
      ImGui::DestroyContext();
    }
  }

  const char *name() const noexcept override { return "ImGuiOverlay"; }

  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Graphics;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.get_texture("BackBuffer", m_backBuffer);
    if (!result) {
      return result;
    }
    result = builder.render(&m_backBuffer, 1);
    if (!result) {
      return result;
    }
    return builder.get_view("BackBufferRTV", m_backBufferRtv);
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    return builder.use_texture(m_backBuffer, RHI::ResourceAccessType::Write,
                               RHI::ResourceState::RenderTarget,
                               RHI::ResourceState::Present);
  }

  void execute(RHI::FrameGraphContext &context) override {
    if (!m_initialized || context.commandContext() == nullptr) {
      return;
    }

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    draw_overlay();
    ImGui::Render();

    RHI::ICommandContext *commandContext = context.commandContext();
    commandContext->set_render_targets(&m_backBufferRtv, 1, {});
    commandContext->set_viewport_scissor(context.width(), context.height());

    auto *commandList = static_cast<ID3D12GraphicsCommandList *>(
        commandContext->native_command_list());
    ID3D12DescriptorHeap *descriptorHeaps[] = {
        m_backend.imgui_srv_descriptor_heap()};
    commandList->SetDescriptorHeaps(1, descriptorHeaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
  }

private:
  static void
  allocate_srv_descriptor(ImGui_ImplDX12_InitInfo *info,
                          D3D12_CPU_DESCRIPTOR_HANDLE *outCpuHandle,
                          D3D12_GPU_DESCRIPTOR_HANDLE *outGpuHandle) {
    auto *backend = static_cast<RHI::DX12::D3D12Backend *>(info->UserData);
    backend->allocate_imgui_srv_descriptor(*outCpuHandle, *outGpuHandle);
  }

  static void free_srv_descriptor(ImGui_ImplDX12_InitInfo *info,
                                  D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                  D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle) {
    auto *backend = static_cast<RHI::DX12::D3D12Backend *>(info->UserData);
    backend->free_imgui_srv_descriptor(cpuHandle, gpuHandle);
  }

  void draw_overlay() {
    const EngineDebugStats debugStats = m_engine.debug_stats();
    const RHI::FrameGraphExecutionStats frameStats =
        m_engine.render_execution_stats();

    bool directionalLightEnabled = debugStats.directionalLightEnabled;

    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(520.0f, 760.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("CueEngineRef GPU Driven Demo");

    ImGui::Text("Frame");
    const float fps = ImGui::GetIO().Framerate;
    ImGui::Text("FPS / Frame Time: %.1f / %.3f ms", fps,
                fps > 0.0f ? 1000.0f / fps : 0.0f);
    ImGui::Text("GPU Frame Time: %s%.3f ms",
                frameStats.hasGpuFrameMs ? "" : "~",
                frameStats.hasGpuFrameMs ? frameStats.gpuFrameMs
                                         : frameStats.totalExecuteMs);
    ImGui::TextDisabled("Object/draw counters are CPU-side estimates until GPU "
                        "readback is added.");

    if (ImGui::CollapsingHeader("Pass GPU Time",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Text("BuildClusterGrid: %.3f ms",
                  pass_gpu_ms(frameStats, {"BuildClusterGrid"}));
      ImGui::Text("PreparePointLights: %.3f ms",
                  pass_gpu_ms(frameStats, {"PreparePointLights"}));
      ImGui::Text("ClusterLightCulling: %.3f ms",
                  pass_gpu_ms(frameStats, {"ClusterLightCulling"}));
      ImGui::Text("ObjectCullAndLod: %.3f ms",
                  pass_gpu_ms(frameStats, {"ObjectCullAndLod"}));
      ImGui::Text("MeshletChunkVisibilityReset: %.3f ms",
                  pass_gpu_ms(frameStats, {"MeshletChunkVisibilityReset"}));
      ImGui::Text("BuildChunkDepthCommands: %.3f ms",
                  pass_gpu_ms(frameStats, {"BuildChunkDepthCommands"}));
      ImGui::Text("ChunkDepthOnlyDraw: %.3f ms",
                  pass_gpu_ms(frameStats, {"ChunkDepthOnlyDraw"}));
      ImGui::Text("GeneratedMeshletDepth: %.3f ms",
                  pass_gpu_ms(frameStats, {"GeneratedMeshletDepthReset",
                                           "GeneratedMeshletDepthCull",
                                           "GeneratedMeshletDepthDispatchArgs",
                                           "GeneratedMeshletDepthStreamBuild",
                                           "GeneratedMeshletDepthArgs",
                                           "GeneratedMeshletDepthDraw"}));
      ImGui::Text(
          "Batching: %.3f ms",
          pass_gpu_ms(frameStats, {"BatchCount", "PrefixSum", "BatchFill"}));
      ImGui::Text("StaticMeshForward: %.3f ms",
                  pass_gpu_ms(frameStats, {"StaticMeshForward"}));
      ImGui::Text("MeshletGroupCull: %.3f ms",
                  pass_gpu_ms(frameStats, {"MeshletGroupCull"}));
      ImGui::Text("VisibilityBuffer: %.3f ms",
                  pass_gpu_ms(frameStats, {"VisibilityBuffer"}));
      ImGui::Text("VisibilityBufferRange: %.3f ms",
                  pass_gpu_ms(frameStats, {"VisibilityBufferRange"}));
      ImGui::Text("VisibilityResolve: %.3f ms",
                  pass_gpu_ms(frameStats, {"VisibilityResolve"}));
      ImGui::Text("VisibilityBufferDebug: %.3f ms",
                  pass_gpu_ms(frameStats, {"VisibilityBufferDebug"}));
      ImGui::Text("VisibilityResolveDebug: %.3f ms",
                  pass_gpu_ms(frameStats, {"VisibilityResolveDebug"}));
    }

    if (ImGui::CollapsingHeader("Clustered Lighting",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
      // ClusterLightCulling が GPU 上で集計した値。
      // pass time と並べて見ることで、cluster grid や compact list の
      // チューニングが効いているか判断する。
      const GpuData::ClusterLightingStatsGpu &clusterStats =
          debugStats.clusterLightingStats;
      const float avgLightsPerCluster =
          clusterStats.clusterCount > 0u
              ? static_cast<float>(clusterStats.totalClusterItems) /
                    static_cast<float>(clusterStats.clusterCount)
              : 0.0f;

      ImGui::Text("BuildClusterGrid: %.3f ms",
                  pass_gpu_ms(frameStats, {"BuildClusterGrid"}));
      ImGui::Text("PreparePointLights: %.3f ms",
                  pass_gpu_ms(frameStats, {"PreparePointLights"}));
      ImGui::Text("ClusterLightCulling: %.3f ms",
                  pass_gpu_ms(frameStats, {"ClusterLightCulling"}));
      ImGui::Text("StaticMeshForward: %.3f ms",
                  pass_gpu_ms(frameStats, {"StaticMeshForward"}));
      ImGui::Separator();
      ImGui::Text("clusterCount: %u", clusterStats.clusterCount);
      ImGui::Text("activeClusterCount: %u", clusterStats.activeClusterCount);
      ImGui::Text("pointLightCount: %u", clusterStats.pointLightCount);
      ImGui::Text("totalClusterItems: %u", clusterStats.totalClusterItems);
      ImGui::Text("avg lights / cluster: %.2f", avgLightsPerCluster);
      ImGui::Text("max lights / cluster: %u", clusterStats.maxLightsInCluster);
      ImGui::Text("overflow clusters: %u", clusterStats.overflowClusterCount);
      ImGui::Text("empty clusters: %u", clusterStats.emptyClusterCount);
      ImGui::Text("reused light lists: %u", clusterStats.reusedListCount);
      ImGui::TextDisabled(
          "Cluster stats are GPU readback values with a small frame delay.");
    }

    if (ImGui::CollapsingHeader("Objects", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Text("total objects: %u", debugStats.totalObjects);
      ImGui::Text("visible objects: %u", debugStats.visibleObjects);
      ImGui::Text("occluded objects: %u", debugStats.occludedObjects);
      ImGui::Text("culled by frustum: %u", debugStats.frustumCulledObjects);
    }

    if (ImGui::CollapsingHeader("Meshlet Visibility",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
      const GpuData::MeshletChunkVisibilityStatsGpu &chunkStats =
          debugStats.meshletChunkVisibilityStats;
      const GpuData::MeshletGroupCullStatsGpu &groupStats =
          debugStats.meshletGroupCullStats;
      ImGui::Text("chunk capacity: %u", debugStats.meshletChunkCapacity);
      ImGui::Text("allocated chunks: %u", debugStats.meshletChunkCount);
      ImGui::Text("visibility words: %u",
                  debugStats.meshletVisibilityWordCount);
      ImGui::Text("depth command count: %u", chunkStats.commandCount);
      ImGui::Text("depth instance count: %u", chunkStats.instanceCount);
      ImGui::Text("current visible chunks: %u",
                  chunkStats.currentVisibleChunkCount);
      ImGui::Text("previous visible chunks: %u",
                  chunkStats.previousVisibleChunkCount);
      ImGui::Text("tested object chunks: %u",
                  chunkStats.testedObjectChunkCount);
      ImGui::Text("rejected by previous visibility: %u",
                  chunkStats.rejectedByPreviousVisibility);
      ImGui::Text("rejected by frustum: %u", chunkStats.rejectedByFrustum);
      ImGui::Text("command overflow: %u", chunkStats.commandOverflowCount);
      ImGui::Text("visible objects (gpu): %u", chunkStats.visibleObjectCount);
      ImGui::Text("culled objects (gpu): %u", chunkStats.culledObjectCount);
      ImGui::Text("rejected by occluder filter: %u",
                  chunkStats.rejectedByOccluderFilter);
      ImGui::Separator();
      ImGui::Text("group candidates: %u", groupStats.candidateObjectCount);
      ImGui::Text("group range objects: %u", groupStats.rangeObjectCount);
      ImGui::Text("group range commands: %u", groupStats.rangeCommandCount);
      ImGui::Text("group tested: %u", groupStats.testedGroupCount);
      ImGui::Text("group rejected by frustum: %u",
                  groupStats.frustumRejectedGroupCount);
      ImGui::Text("cone tested meshlets: %u",
                  groupStats.coneTestedMeshletCount);
      ImGui::Text("cone rejected meshlets: %u",
                  groupStats.coneRejectedMeshletCount);
      ImGui::TextDisabled(
          "Chunk-depth commands are filtered and generated for measurement; "
          "meshlet group range draws are conservative and may fall back to "
          "normal batching.");
    }

    if (ImGui::CollapsingHeader("Draw", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Text("indirect draw count: %u", debugStats.indirectDrawCount);
      ImGui::Text("instance count: %u", debugStats.instanceCount);
      ImGui::Text("triangle estimate: %llu",
                  static_cast<unsigned long long>(
                      debugStats.submittedTriangleEstimate));
    }

    if (ImGui::CollapsingHeader("LOD Distribution",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Text("LOD0: %u", debugStats.lodObjectCounts[0]);
      ImGui::Text("LOD1: %u", debugStats.lodObjectCounts[1]);
      ImGui::Text("LOD2: %u", debugStats.lodObjectCounts[2]);
      ImGui::Text("LOD3: %u", debugStats.lodObjectCounts[3]);
      ImGui::Text("LOD4: %u", debugStats.lodObjectCounts[4]);
      ImGui::Text("impostor: %u", debugStats.impostorCount);
    }

    if (ImGui::CollapsingHeader("Toggles", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Checkbox("Frustum Culling", &m_frustumCullingEnabled);
      ImGui::Checkbox("LOD Selection", &m_lodEnabled);
      ImGui::Checkbox("Impostor", &m_impostorEnabled);
      if (ImGui::Checkbox("Directional Light", &directionalLightEnabled)) {
        m_engine.set_directional_light_enabled(directionalLightEnabled);
      }
      ImGui::Checkbox("Point Lights", &m_pointLightsEnabled);
      ImGui::TextDisabled("Only Directional Light is wired to the renderer.");
    }

    if (ImGui::CollapsingHeader("Camera / Debug",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
      const char *renderPathItems[] = {"Forward", "Visibility Buffer"};
      int renderPathIndex = static_cast<int>(m_engine.render_path());
      if (ImGui::Combo("Render Path", &renderPathIndex, renderPathItems,
                       IM_ARRAYSIZE(renderPathItems))) {
        m_engine.set_render_path(
            static_cast<DrawSystem::RenderPath>(renderPathIndex));
      }

      const char *debugViewItems[] = {"Forward",
                                      "Visibility ObjectId",
                                      "Visibility PrimitiveId",
                                      "Visibility Barycentric",
                                      "Visibility Normal",
                                      "Visibility UV",
                                      "Visibility Lit",
                                      "Visibility Material",
                                      "Render Path"};
      int debugViewIndex = static_cast<int>(m_engine.render_debug_view());
      if (ImGui::Combo("Render Debug View", &debugViewIndex, debugViewItems,
                       IM_ARRAYSIZE(debugViewItems))) {
        m_engine.set_render_debug_view(
            static_cast<DrawSystem::RenderDebugView>(debugViewIndex));
      }
      ImGui::Text("camera position: %.2f, %.2f, %.2f",
                  debugStats.cameraPosition.x, debugStats.cameraPosition.y,
                  debugStats.cameraPosition.z);
      ImGui::Text("observer view: %s", g_observerViewEnabled ? "ON" : "OFF");
      ImGui::Text("camera control: %s",
                  g_controlObserverCamera ? "observer" : "main/culling");
      ImGui::Text("visible cells / total cells: %u / %u",
                  debugStats.visibleCells, debugStats.totalCells);
      ImGui::Text("selected depth bin: %u", debugStats.selectedDepthBin);
    }

    if (ImGui::CollapsingHeader("Render Cost",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Text("submitted triangles: %llu",
                  static_cast<unsigned long long>(
                      debugStats.submittedTriangleEstimate));
      ImGui::Text(
          "saved triangles estimate: %llu",
          static_cast<unsigned long long>(debugStats.savedTriangleEstimate));
      ImGui::Text("saved objects estimate: %u", debugStats.savedObjectEstimate);
    }

    if (ImGui::CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::BulletText("W/A/S/D: move camera");
      ImGui::BulletText("Space / Ctrl: move up / down");
      ImGui::BulletText("Shift: fast movement");
      ImGui::BulletText("Right mouse drag: look around");
      ImGui::BulletText("C: toggle observer view");
      ImGui::BulletText("Tab: switch main / observer camera");
      ImGui::BulletText("P: toggle render path");
      ImGui::BulletText("V: cycle render debug view");
      ImGui::BulletText("Mouse over this window: operate ImGui");
    }

    ImGui::End();
  }

  RHI::DX12::D3D12Backend &m_backend;
  Engine &m_engine;
  RHI::TextureHandle m_backBuffer{};
  RHI::ViewHandle m_backBufferRtv{};
  bool m_initialized = false;
  bool m_frustumCullingEnabled = true;
  bool m_lodEnabled = true;
  bool m_impostorEnabled = true;
  bool m_pointLightsEnabled = true;
};

} // namespace

// windows アプリのエントリーポイント
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  // パラメーター
  uint32_t width = 1920;
  uint32_t height = 1080;
  const char *className = "CueEditorWindowClass";
  const char *title = "Cue Editor";
  uint32_t maxFps = 0;
  uint32_t bufferCount = 3;
  const Math::uint3 modelGridCount(100u, 2u, 100u);
  const float modelTargetRadius = 0.6f;
  const uint32_t maxPointLightCount = 5000;
  const bool enableDirectionalLight = false;

  // 処理結果
  Result r = Result::ok();

  // プラットフォーム実装を初期化
  std::unique_ptr<PAL::Win::WinPlatform> platform =
      std::make_unique<PAL::Win::WinPlatform>();
  std::unique_ptr<Core::CQRS::Bridge> commandBridge =
      std::make_unique<Core::CQRS::Bridge>();
  platform->set_command_bridge(
      commandBridge.get()); // コマンドブリッジをプラットフォームにセット
  PAL::PlatformSetupInfo setupInfo{};
  setupInfo.width = width;
  setupInfo.height = height;
  setupInfo.className = className;
  setupInfo.title = title;
  r = platform->initialize(setupInfo);

  // 失敗したらエラーを表示して終了
  if (!r) {
    CUE_ASSERT_FORMAT(false, "Failed to initialize platform: %s",
                      r.message.data());
    Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                  "Failed to initialize platform: %s", r.message.data());
    return -1;
  }

  // Logger にプラットフォームのファイルシステムをセット
  Core::IO::set_log_file(platform->file_system(),
                         Core::IO::Path("logs/editor.log"), true);

  // PerformanceCounter を初期化
  Core::PerformanceCounter profiler(platform->clock());

  // レンダーバックエンドを初期化
  std::unique_ptr<RHI::DX12::D3D12Backend> renderBackend =
      std::make_unique<RHI::DX12::D3D12Backend>();
  RHI::RenderBackendSetupInfo renderBackendSetupInfo{};
#ifdef CUE_DEBUG
  bool enableDebugLayer = true;
#else
  bool enableDebugLayer = false;
#endif
  renderBackendSetupInfo.enableDebugLayer = enableDebugLayer;
  renderBackendSetupInfo.width = width;
  renderBackendSetupInfo.height = height;
  renderBackendSetupInfo.bufferCount = bufferCount;
  renderBackend->set_win_platform(
      platform.get()); // Windows プラットフォームをバックエンドにセット
  r = renderBackend->initialize(renderBackendSetupInfo);

  // 失敗したらエラーを表示して終了
  if (!r) {
    CUE_ASSERT_FORMAT(false, "Failed to initialize render backend: %s",
                      r.message.data());
    Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                  "Failed to initialize render backend: %s", r.message.data());
    return -1;
  }

  // Engine を初期化
  std::unique_ptr<Engine> engine = std::make_unique<Engine>();
  std::unique_ptr<RHI::FrameGraphPass> imguiOverlayPass =
      std::make_unique<ImGuiOverlayPass>(platform->get_window_handle(),
                                         *renderBackend, *engine);
  platform->set_message_handler([](HWND hwnd, UINT message, WPARAM wParam,
                                   LPARAM lParam, LRESULT &outResult) -> bool {
    outResult = ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam);
    return outResult != 0;
  });

  EngineSetupInfo engineSetupInfo{};
  engineSetupInfo.maxFps = maxFps; // 最大フレームレートを Engine にセット
  engineSetupInfo.maxPointLightCount = maxPointLightCount;
  engineSetupInfo.enableDirectionalLight = enableDirectionalLight;
  engineSetupInfo.editorPass = std::move(imguiOverlayPass);
  engineSetupInfo.platform =
      platform.get(); // プラットフォームを Engine にセット
  engineSetupInfo.platformCommandBridge =
      commandBridge.get(); // コマンドブリッジを Engine にセット
  engineSetupInfo.renderBackend =
      renderBackend.get(); // レンダーバックエンドを Engine にセット
  r = engine->initialize(engineSetupInfo);
  if (!r) {
    Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                  "Failed to initialize engine: %s", r.message.data());
    CUE_ASSERT_FORMAT(false, "Failed to initialize engine: %s",
                      r.message.data());
    return -1;
  }

  Editor::DebugCamera debugCamera{};
  Editor::DebugCamera observerCamera{
      Math::float3(-12.0f, 6.0f, -12.0f),
      45.0f * Editor::DebugCameraConstants::k_pi / 180.0f, -0.35f};
  r = engine->set_view_projection(
      debugCamera.make_view_projection(width, height));
  if (!r) {
    CUE_ASSERT_FORMAT(false, "Failed to set debug camera: %s",
                      r.message.data());
    Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                  "Failed to set debug camera: %s", r.message.data());
    return -1;
  }

  struct TestModelDesc final {
    const char *fileName = nullptr;
    const char *modelName = nullptr;
    uint32_t lodGroupIndex = 0;
  };

  constexpr std::array<Editor::ModelImporter::LodGroupSettings, 4> k_lodGroups =
      {
          Editor::ModelImporter::LodGroupSettings{
              "StanfordDragon", {0.50f, 0.15f, 0.01f}, true},
          Editor::ModelImporter::LodGroupSettings{
              "AsianDragon", {0.25f, 0.05f, 0.005f}, true},
          Editor::ModelImporter::LodGroupSettings{
              "Bunny", {0.50f, 0.15f, 0.01f}, true},
          Editor::ModelImporter::LodGroupSettings{
              "Buddha", {0.20f, 0.03f, 0.003f}, true},
      };

  constexpr std::array<TestModelDesc, 4> k_testModels = {
      TestModelDesc{"stanforddragon.obj", "stanforddragon", 0u},
      TestModelDesc{"asiandragon.obj", "asiandragon", 1u},
      TestModelDesc{"bunny.obj", "bunny", 2u},
      TestModelDesc{"buddha.obj", "buddha", 3u},
  };

  std::vector<Core::Native::ModelData> modelDataList{};
  modelDataList.reserve(k_testModels.size());
  for (const TestModelDesc &modelDesc : k_testModels) {
    Core::Native::ModelData modelData{};
    const Core::IO::Path modelPath(std::string(CUE_PROJECT_ROOT_PATH) +
                                   "/TestProject/Assets/Models/" +
                                   modelDesc.fileName);
    if (modelDesc.lodGroupIndex >= k_lodGroups.size()) {
      CUE_ASSERT_FORMAT(false, "Invalid LOD group index for model '%s'.",
                        modelDesc.modelName);
      return -1;
    }

    r = Editor::ModelImporter::import_model(
        modelPath, modelDesc.modelName, k_lodGroups[modelDesc.lodGroupIndex],
        modelData);
    if (!r) {
      CUE_ASSERT_FORMAT(false, "Failed to import model '%s': %s",
                        modelDesc.modelName, r.message.data());
      Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                    "Failed to import model '{}': {}", modelDesc.modelName,
                    r.message);
      return -1;
    }
    modelDataList.push_back(std::move(modelData));
  }

  r = engine->register_models(modelDataList, modelGridCount, modelTargetRadius);
  if (!r) {
    CUE_ASSERT_FORMAT(false, "Failed to register test models: %s",
                      r.message.data());
    Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                  "Failed to register test models: %s", r.message.data());
    return -1;
  }

  // ウィンドウ表示を開始
  r = platform->start();
  if (!r) {
    CUE_ASSERT_FORMAT(false, "Failed to start platform: %s", r.message.data());
    Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                  "Failed to start platform: %s", r.message.data());
    return -1;
  }

  // プロセスメモリ、システムメモリ使用量をログに出力
  PAL::ProcessMemoryUsage processMemoryUsage{};
  PAL::SystemMemoryUsage systemMemoryUsage{};
  if (r = platform->get_process_memory_usage(processMemoryUsage); r) {
    Core::IO::log(
        Core::IO::LogSink::console | Core::IO::LogSink::file,
        "Process Memory Usage - Working Set: {} MB, Private Bytes: {} MB",
        processMemoryUsage.workingSetBytes / (1024 * 1024),
        processMemoryUsage.privateBytes / (1024 * 1024));
  } else {
    Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                  "Failed to get process memory usage: {}", r.message);
  }
  if (r = platform->get_system_memory_usage(systemMemoryUsage); r) {
    Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                  "System Memory Usage - Total Phys: {} MB, Avail Phys: {} MB, "
                  "Memory Load: {}%",
                  systemMemoryUsage.totalPhysBytes / (1024 * 1024),
                  systemMemoryUsage.availPhysBytes / (1024 * 1024),
                  systemMemoryUsage.memoryLoadPercent);
  } else {
    Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                  "Failed to get system memory usage: {}", r.message);
  }
  // GPU
  RHI::GpuMemoryUsage gpuMemoryUsage{};
  if (r = renderBackend->get_gpu_memory_usage(gpuMemoryUsage); r) {
    Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                  "GPU Memory Usage - Budget: {} MB, Current Usage: {} MB, "
                  "Available for "
                  "Reservation: {} MB, Current Reservation: {} MB",
                  gpuMemoryUsage.budgetBytes / (1024 * 1024),
                  gpuMemoryUsage.currentUsageBytes / (1024 * 1024),
                  gpuMemoryUsage.availableForReservationBytes / (1024 * 1024),
                  gpuMemoryUsage.currentReservationBytes / (1024 * 1024));
  } else {
    Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                  "Failed to get GPU memory usage: {}", r.message);
  }

  // メインループ
  static constexpr bool kEnablePeriodicFrameLogs = false;
  static constexpr bool kEnableRuntimeTimingCollectors = false;
  static constexpr uint64_t kPeriodicFrameLogInterval = 120u;
  bool isRunning = true;
  auto previousInputTime = std::chrono::steady_clock::now();
  uint64_t nextRenderStatsLogFrame = 1u;
  PassTimingCollector passTimingCollector{};
  RenderWorkloadCollector renderWorkloadCollector{};
  while (isRunning) {
    CpuFrameTiming cpuTiming{};
    const TimingPoint frameTimingBegin = TimingClock::now();

    TimingPoint segmentBegin = TimingClock::now();
    const auto currentInputTime = segmentBegin;
    const float deltaSeconds = std::clamp(
        std::chrono::duration<float>(currentInputTime - previousInputTime)
            .count(),
        0.0f, 0.1f);
    previousInputTime = currentInputTime;
    cpuTiming.setupMs = elapsed_ms(segmentBegin, TimingClock::now());

    // プラットフォームメッセージを処理
    segmentBegin = TimingClock::now();
    PAL::PlatformMessage message = platform->poll_message();
    if (message == PAL::PlatformMessage::Quit) {
      isRunning = false;
    }
    cpuTiming.pollMessageMs = elapsed_ms(segmentBegin, TimingClock::now());

    // フレーム開始
    segmentBegin = TimingClock::now();
    r = platform->begin_frame();
    cpuTiming.platformBeginMs = elapsed_ms(segmentBegin, TimingClock::now());

    // 失敗したらエラーを表示して終了
    if (!r) {
      CUE_ASSERT_FORMAT(false, "Failed to begin frame: %s", r.message.data());
      return -1;
    }

    segmentBegin = TimingClock::now();
    r = engine->begin_frame();
    cpuTiming.engineBeginMs = elapsed_ms(segmentBegin, TimingClock::now());

    // 失敗したらエラーを表示して終了
    if (!r) {
      CUE_ASSERT_FORMAT(false, "Failed to begin engine frame: %s",
                        r.message.data());
      return -1;
    }

    segmentBegin = TimingClock::now();
    if (was_key_pressed('C')) {
      g_observerViewEnabled = !g_observerViewEnabled;
    }
    if (was_key_pressed(VK_TAB)) {
      g_controlObserverCamera = !g_controlObserverCamera;
      g_observerViewEnabled = g_controlObserverCamera;
    }
    if (was_key_pressed('V')) {
      engine->set_render_debug_view(
          next_render_debug_view(engine->render_debug_view()));
      Core::IO::log(Core::IO::LogSink::console, "Render debug view: {}",
                    render_debug_view_name(engine->render_debug_view()));
    }
    if (was_key_pressed('P')) {
      const DrawSystem::RenderPath nextPath =
          engine->render_path() == DrawSystem::RenderPath::Forward
              ? DrawSystem::RenderPath::VisibilityBuffer
              : DrawSystem::RenderPath::Forward;
      engine->set_render_path(nextPath);
      Core::IO::log(Core::IO::LogSink::console, "Render path: {}",
                    render_path_name(engine->render_path()));
    }

    Editor::DebugCamera::Input cameraInput =
        make_debug_camera_input(platform->get_window_handle(), deltaSeconds);
    if (g_controlObserverCamera) {
      observerCamera.update(cameraInput);
    } else {
      debugCamera.update(cameraInput);
    }

    const GpuData::ViewProjectionGpu mainViewProjection =
        debugCamera.make_view_projection(width, height);
    r = engine->set_view_projection(mainViewProjection);
    if (!r) {
      CUE_ASSERT_FORMAT(false, "Failed to update debug camera: %s",
                        r.message.data());
      return -1;
    }
    if (g_observerViewEnabled) {
      r = engine->set_render_view_projection(
          observerCamera.make_view_projection(width, height));
    } else {
      r = engine->set_render_view_projection(mainViewProjection);
    }
    if (!r) {
      CUE_ASSERT_FORMAT(false, "Failed to update render camera: %s",
                        r.message.data());
      return -1;
    }
    cpuTiming.inputCameraMs = elapsed_ms(segmentBegin, TimingClock::now());

    // --- ここで Engine 側の更新と描画処理を呼び出す ---
    segmentBegin = TimingClock::now();
    r = engine->tick();
    cpuTiming.engineTickMs = elapsed_ms(segmentBegin, TimingClock::now());

    // 失敗したらエラーを表示して終了
    if (!r) {
      CUE_ASSERT_FORMAT(false, "Failed to tick engine: %s", r.message.data());
      return -1;
    }

    const Core::Time::FrameCounter &frameCounter =
        engine->frame_controller().frame_counter();
    if (frameCounter.total_frames() > 0) {
      // Core::IO::log(Core::IO::LogSink::console, "FPS : {:.2f}",
      // frameCounter.fps());
    }
    /*profiler.begin("Test", "Update");
    profiler.end("Test", "Update");
 if
    (const auto snapshot = profiler.get_snapshot("Test", "Update"))
    {
        Core::IO::log(Core::IO::LogSink::console, "Update Time : {:.2f} ms",
    snapshot->timer.elapsed_seconds() * 1000.0);
    }*/

    segmentBegin = TimingClock::now();
    r = engine->end_frame();
    cpuTiming.engineEndMs = elapsed_ms(segmentBegin, TimingClock::now());

    // 失敗したらエラーを表示して終了
    if (!r) {
      CUE_ASSERT_FORMAT(false, "Failed to end engine frame: %s",
                        r.message.data());
      return -1;
    }

    // フレーム終了
    segmentBegin = TimingClock::now();
    r = platform->end_frame();
    cpuTiming.platformEndMs = elapsed_ms(segmentBegin, TimingClock::now());

    // 失敗したらエラーを表示して終了
    if (!r) {
      CUE_ASSERT_FORMAT(false, "Failed to end frame: %s", r.message.data());
      return -1;
    }

    cpuTiming.totalMs = elapsed_ms(frameTimingBegin, TimingClock::now());
    const RHI::FrameGraphExecutionStats frameStats =
        engine->render_execution_stats();
    const RHI::FrameGraphExecutionStats presentFrameStats =
        engine->present_execution_stats();
    const EngineDebugStats debugStats = engine->debug_stats();
    if constexpr (kEnableRuntimeTimingCollectors) {
      passTimingCollector.add_frame("Render", frameStats);
      passTimingCollector.add_frame("Present", presentFrameStats);
      renderWorkloadCollector.add_frame(debugStats);
    }
    if constexpr (kEnablePeriodicFrameLogs) {
      const uint64_t completedFrameCount = frameCounter.total_frames();
      if (completedFrameCount >= nextRenderStatsLogFrame) {
        log_render_debug_stats(completedFrameCount, frameCounter.fps(),
                               debugStats, frameStats);
        log_frame_timing(completedFrameCount, frameCounter.fps(), cpuTiming,
                         frameStats);
        nextRenderStatsLogFrame =
            completedFrameCount + kPeriodicFrameLogInterval;
      }
    }
  }

  if constexpr (kEnableRuntimeTimingCollectors) {
    renderWorkloadCollector.log_summary();
    passTimingCollector.log_summary();
  }
  Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                "Editor shutdown");
  Core::IO::clear_log_file();

  // 終了処理
  engine->shutdown();
  engine.reset();
  renderBackend->shutdown();
  renderBackend.reset();
  platform->shutdown();
  platform.reset();

  return 0;
}
