#pragma once

/// ********************************************************************************
/// エンジン
/// ********************************************************************************

// === Base includes ===
#include <CueResult.h>

// === Core includes ===
#include <CQRS/CQRS.h>

// === PAL includes ===
#include <PAL.h>
#include <PlatformRuntimeState.h>

// === RHI includes ===
#include <FrameGraph.h>
#include <RHI.h>
#include <RHIUtils.h>

// === Engine includes ===
#include "DrawSystem/DrawFrameState.h"
#include "DrawSystem/DrawResources.h"
#include "DrawSystem/MeshPool.h"
#include "DrawSystem/RenderDebugView.h"
#include "DrawSystem/RenderPath.h"
#include "FrameController.h"
#include "GpuData/Batching.h"
#include "GpuData/ClusteredLighting.h"
#include "GpuData/Transform.h"
#include "GpuData/ViewProjection.h"
#include "LightingSystem/GpuData/LightData.h"
#include "LightingSystem/LightResources.h"

// === C++ includes ===
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace Cue {
struct EngineSetupInfo final {
  PAL::IPlatform *platform = nullptr; // プラットフォームインターフェース
  RHI::IRenderBackend *renderBackend = nullptr; // レンダーバックエンド
  Core::CQRS::Bridge *platformCommandBridge =
      nullptr;          // プラットフォームからコマンドを受け取るためのブリッジ
  uint32_t maxFps = 60; // 最大フレームレート
  uint32_t maxPointLightCount = 64;   // ポイントライトの最大数
  bool enableDirectionalLight = true; // DirectionalLight を有効化するか
  std::unique_ptr<RHI::FrameGraphPass> editorPass{};
};

struct EngineDebugStats final {
  uint32_t totalObjects = 0;
  uint32_t totalCells = 0;
  uint32_t visibleCells = 0;
  uint32_t visibleObjects = 0;
  uint32_t occludedObjects = 0;
  uint32_t frustumCulledObjects = 0;
  uint32_t indirectDrawCount = 0;
  uint32_t instanceCount = 0;
  uint64_t submittedTriangleEstimate = 0;
  uint64_t savedTriangleEstimate = 0;
  uint32_t savedObjectEstimate = 0;
  std::array<uint32_t, 5> lodObjectCounts{0, 0, 0, 0, 0};
  uint32_t impostorCount = 0;
  bool frustumCullingEnabled = true;
  bool lodEnabled = true;
  bool impostorEnabled = true;
  bool directionalLightEnabled = true;
  bool pointLightsEnabled = true;
  uint32_t pointLightCount = 0;
  uint32_t meshletChunkCapacity = 0;
  uint32_t meshletChunkCount = 0;
  uint32_t meshletVisibilityWordCount = 0;
  uint32_t currentVisibleMeshletChunkCount = 0;
  uint32_t previousVisibleMeshletChunkCount = 0;
  GpuData::ObjectCullLodStatsGpu objectCullLodStats{};
  GpuData::MeshletChunkVisibilityStatsGpu meshletChunkVisibilityStats{};
  GpuData::MeshletGroupCullStatsGpu meshletGroupCullStats{};
  GpuData::StaticMeshBatchStatsGpu staticMeshBatchStats{};
  GpuData::ClusterLightingStatsGpu clusterLightingStats{};
  Math::float3 cameraPosition = Math::float3::zero();
  uint32_t selectedDepthBin = 0;
};

class Engine final {
public:
  Engine() = default;
  // コピー禁止
  Engine(const Engine &) = delete;
  Engine &operator=(const Engine &) = delete;
  // ムーブ禁止
  Engine(Engine &&) = delete;
  Engine &operator=(Engine &&) = delete;
  ~Engine() = default;

  /// @brief 初期化
  Result initialize(EngineSetupInfo &a_info);

  /// @brief 終了
  void shutdown();

  /// @brief フレーム開始処理
  Result begin_frame();

  /// @brief フレーム終了処理
  Result end_frame();

  /// @brief ティック処理
  Result tick();

  /// @brief インポート済みモデルを描画用 MeshPool に登録する
  Result register_model(const Core::Native::ModelData &a_modelData);
  Result register_model(const Core::Native::ModelData &a_modelData,
                        Math::uint3 a_instanceCounts);
  Result register_model(const Core::Native::ModelData &a_modelData,
                        Math::uint3 a_instanceCounts, float a_targetRadius);
  Result
  register_models(const std::vector<Core::Native::ModelData> &a_modelDataList,
                  Math::uint3 a_instanceCounts, float a_targetRadius);

  /// @brief DebugCamera など外部で作った ViewProjection を描画へ渡す
  Result
  set_view_projection(const GpuData::ViewProjectionGpu &a_viewProjection);
  /// @brief 表示専用カメラを設定する。未設定時は culling 用カメラと同じ値を使う
  Result set_render_view_projection(
      const GpuData::ViewProjectionGpu &a_viewProjection);
  [[nodiscard]] EngineDebugStats debug_stats() const noexcept;
  [[nodiscard]] RHI::FrameGraphExecutionStats
  render_execution_stats() const noexcept;
  [[nodiscard]] RHI::FrameGraphExecutionStats
  present_execution_stats() const noexcept;
  void set_directional_light_enabled(bool enabled) noexcept;
  void set_render_debug_view(DrawSystem::RenderDebugView view) noexcept {
    m_renderDebugView = view;
  }
  [[nodiscard]] DrawSystem::RenderDebugView render_debug_view() const noexcept {
    return m_renderDebugView;
  }
  void set_render_path(DrawSystem::RenderPath path) noexcept {
    m_renderPath = path;
  }
  [[nodiscard]] DrawSystem::RenderPath render_path() const noexcept {
    return m_renderPath;
  }

  //
  FrameController &frame_controller() noexcept { return *m_frameController; }

private:
  /// @brief 更新
  std::function<void(uint64_t, uint32_t)> update() {
    return [this](uint64_t frameNo, uint32_t updateIndex) {
      frameNo;
      updateIndex; // 未使用パラメーターの警告回避
    };
  }
  /// @brief 描画
  std::function<void(uint64_t, uint32_t)> render();
  /// @brief present
  std::function<void(uint64_t, uint32_t)> present();
  Result create_frame_graphs(std::unique_ptr<RHI::FrameGraphPass> a_editorPass);
  Result register_model_set(
      const std::vector<const Core::Native::ModelData *> &a_modelDataList,
      Math::uint3 a_instanceCounts, float a_targetRadius);
  Result commit_static_draw_data_to_uploaders();
  Result commit_view_projection_to_uploaders();
  Result commit_render_view_projection_to_uploaders();
  Result commit_light_data_to_uploaders();

private:
  std::unique_ptr<FrameController> m_frameController =
      nullptr; // フレームコントローラー
  PAL::IPlatform *m_platform =
      nullptr; // プラットフォームインターフェースの非所有ポインタ
  Core::CQRS::Bridge *m_platformCommandBridge =
      nullptr; // プラットフォームからコマンドを受け取るためのブリッジ
  PAL::PlatformRuntimeState
      m_platformRuntimeState; // プラットフォームランタイム状態
  RHI::IRenderBackend *m_renderBackend =
      nullptr; // レンダーバックエンドの非所有ポインタ

  std::unique_ptr<RHI::FrameGraph> m_frameGraph = nullptr;
  std::unique_ptr<RHI::FrameGraph> m_presentFrameGraph = nullptr;

  // --- 全体共有リソース ---
  RHI::RenderTargetResources m_gameRenderTarget{};

  // --- サブシステム ---
  std::unique_ptr<DrawSystem::MeshPool> m_meshPool = nullptr;
  std::unique_ptr<DrawSystem::DrawResources> m_drawResources = nullptr;
  std::unique_ptr<LightingSystem::LightResources> m_lightResources = nullptr;
  DrawSystem::DrawFrameState m_drawFrameState{};
  GpuData::ViewProjectionGpu m_viewProjection{};
  GpuData::ViewProjectionGpu m_renderViewProjection{};
  GpuData::MaterialGpu m_material{};
  GpuData::LightFrameGpu m_lightFrame{};
  GpuData::DirectionalLightGpu m_directionalLight{};
  uint32_t m_bufferCount = 1;
  uint32_t m_maxObjectCount = 0;
  uint32_t m_maxCellCount = 0;
  uint32_t m_maxMeshletChunkCount = 0;
  uint32_t m_maxPointLightCount = 0;
  uint32_t m_pointLightBufferCapacity = 1;
  bool m_enableDirectionalLight = true;
  uint32_t m_drawMeshId = UINT32_MAX;
  std::array<uint32_t, 5> m_drawLodMeshIds{UINT32_MAX, UINT32_MAX, UINT32_MAX,
                                           UINT32_MAX, UINT32_MAX};
  uint32_t m_drawLodCount = 0;
  uint32_t m_drawObjectCount = 0;
  uint32_t m_drawCellCount = 0;
  uint64_t m_staticDrawUploadVersion = 0;
  uint64_t m_viewProjectionUploadVersion = 0;
  uint64_t m_renderViewProjectionUploadVersion = 0;
  uint64_t m_lightUploadVersion = 0;
  bool m_hasDrawableObject = false;
  std::vector<RHI::MeshHandle> m_meshHandles{};
  std::vector<GpuData::RenderableInfo> m_renderableInfos{};
  std::vector<GpuData::RenderCellGpu> m_renderCells{};
  std::vector<GpuData::ObjectTransformGpu> m_objectTransforms{};
  std::vector<GpuData::PointLightGpu> m_pointLights{};
  DrawSystem::RenderPath m_renderPath = DrawSystem::RenderPath::VisibilityBuffer;
  DrawSystem::RenderDebugView m_renderDebugView =
      DrawSystem::RenderDebugView::Forward;
  EngineDebugStats m_debugStats{};
};
} // namespace Cue
