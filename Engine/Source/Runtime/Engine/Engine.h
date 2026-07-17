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
#include "DrawSystem/RenderFeatureSettings.h"
#include "FrameController.h"
#include "GpuData/Batching.h"
#include "GpuData/Transform.h"
#include "GpuData/ViewProjection.h"
#include "LightingSystem/GpuData/LightData.h"
#include "LightingSystem/LightResources.h"

// === C++ includes ===
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace Cue
{
struct EngineSetupInfo final
{
    PAL::IPlatform *platform = nullptr;
    RHI::IRenderBackend *renderBackend = nullptr;
    Core::CQRS::Bridge *platformCommandBridge =
        nullptr;
    uint32_t maxFps = 60;
    uint32_t maxPointLightCount = 64;
    bool enableDirectionalLight = true;
    std::unique_ptr<RHI::FrameGraphPass> rendererPass{};
};

class Engine final
{
  public:
    Engine() = default;
    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;
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
    Result register_models(
        const std::vector<Core::Native::ModelData> &a_modelDataList,
        Math::uint3 a_instanceCounts, float a_targetRadius);

    /// @brief DebugCamera など外部で作った ViewProjection を描画へ渡す
    Result set_view_projection(
        const GpuData::ViewProjectionGpu &a_viewProjection);
    [[nodiscard]] bool directional_light_enabled() const noexcept
    {
        return m_enableDirectionalLight;
    }
    [[nodiscard]] DrawSystem::RenderComparisonMode render_comparison_mode()
        const noexcept;
    [[nodiscard]] const DrawSystem::RenderFeatureSettings&
    render_feature_settings() const noexcept;
    [[nodiscard]] DrawSystem::RenderDebugViewMode render_debug_view_mode()
        const noexcept;
    void set_render_comparison_mode(
        DrawSystem::RenderComparisonMode mode) noexcept;
    void set_render_debug_view_mode(
        DrawSystem::RenderDebugViewMode mode) noexcept;
    void set_directional_light_enabled(bool enabled) noexcept;

    FrameController &frame_controller() noexcept
    {
        return *m_frameController;
    }

  private:
    /// @brief 更新
    std::function<void(uint64_t, uint32_t)> update()
    {
        return [this](uint64_t frameNo, uint32_t updateIndex)
        {
            static_cast<void>(frameNo);
            static_cast<void>(updateIndex);
        };
    }
    /// @brief 描画
    std::function<void(uint64_t, uint32_t)> render();
    /// @brief present
    std::function<void(uint64_t, uint32_t)> present();
    Result create_frame_graphs(
        std::unique_ptr<RHI::FrameGraphPass> a_rendererPass);
    Result register_model_set(
        const std::vector<const Core::Native::ModelData *> &a_modelDataList,
        Math::uint3 a_instanceCounts, float a_targetRadius);
    Result commit_static_draw_data_to_uploaders();
    Result commit_view_projection_to_uploaders();
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

    // Upload heap への commit 完了を FrameGraph の static copy pass へ通知する。
    // revision は全 slice の書き込み後に進めるため、copy pass は現在の slice から
    // single default heap へ一度だけ転送すればよい。
    std::atomic<uint64_t> m_staticDrawUploadRevision{0};
    std::atomic<uint64_t> m_lightUploadRevision{0};

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
    GpuData::MaterialGpu m_material{};
    GpuData::LightFrameGpu m_lightFrame{};
    GpuData::DirectionalLightGpu m_directionalLight{};
    uint32_t m_bufferCount = 1;
    uint32_t m_maxObjectCount = 0;
    uint32_t m_maxCellCount = 0;
    uint32_t m_maxPointLightCount = 0;
    uint32_t m_pointLightBufferCapacity = 1;
    bool m_enableDirectionalLight = true;
    DrawSystem::RenderFeatureSettings m_renderFeatureSettings =
        DrawSystem::render_feature_settings_for_mode(
            DrawSystem::RenderComparisonMode::Final);
    uint32_t m_drawMeshId = UINT32_MAX;
    std::array<uint32_t, 5> m_drawLodMeshIds{UINT32_MAX, UINT32_MAX, UINT32_MAX,
                                             UINT32_MAX, UINT32_MAX};
    uint32_t m_drawLodCount = 0;
    uint32_t m_drawObjectCount = 0;
    uint32_t m_drawCellCount = 0;
    bool m_hasDrawableObject = false;
    std::vector<RHI::MeshHandle> m_meshHandles{};
    std::vector<GpuData::RenderableInfo> m_renderableInfos{};
    std::vector<GpuData::RenderCellGpu> m_renderCells{};
    std::vector<GpuData::ObjectTransformGpu> m_objectTransforms{};
    std::vector<GpuData::PointLightGpu> m_pointLights{};
};
} // namespace Cue
