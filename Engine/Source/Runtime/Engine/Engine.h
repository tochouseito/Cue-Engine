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
#include "FrameController.h"

#include "DrawSystem/DrawFrameState.h"
#include "DrawSystem/DrawResources.h"
#include "DrawSystem/DrawScene.h"
#include "DrawSystem/MeshPool.h"
#include "GameCore/GameWorld.h"

// === C++ includes ===
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace Cue
{
struct EngineSetupInfo final
{
    PAL::IPlatform* platform = nullptr;           // プラットフォームインターフェース
    RHI::IRenderBackend* renderBackend = nullptr; // レンダーバックエンド
    std::unique_ptr<RHI::FrameGraphPass> editorPass = nullptr;
    const DrawSystem::RenderView* debugRenderView = nullptr; // Editor DebugView 用の描画視点
    Core::CQRS::Bridge* platformCommandBridge = nullptr; // プラットフォームからコマンドを受け取るためのブリッジ
    uint32_t maxFps = 60;                                // 最大フレームレート
};

class Engine final
{
    public:
    Engine() = default;
    // コピー禁止
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    // ムーブ禁止
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;
    ~Engine() = default;

    /// @brief 初期化
    Result initialize(EngineSetupInfo& a_info);

    /// @brief 終了
    void shutdown();

    /// @brief フレーム開始処理
    Result begin_frame();

    /// @brief フレーム終了処理
    Result end_frame();

    /// @brief ティック処理
    Result tick();

    FrameController& frame_controller() noexcept
    {
        return *m_frameController;
    }

    GameCore::GameWorld& game_world() noexcept
    {
        return m_gameWorld;
    }

    /// @brief GameCore camera の代わりに使う描画視点を設定する。
    void set_render_view_override(const DrawSystem::RenderView& a_renderView) noexcept;

    /// @brief 外部から設定した描画視点 override を解除する。
    void clear_render_view_override() noexcept;

    private:
    /// @brief 更新
    std::function<void(uint64_t, uint32_t)> update();
    /// @brief 描画
    std::function<void(uint64_t, uint32_t)> render();
    /// @brief present
    std::function<void(uint64_t, uint32_t)> present();
    Result create_frame_graphs(std::unique_ptr<RHI::FrameGraphPass> a_editorPass);

    /// @brief DrawSystem 用の ECS 抽出 pipeline を構築する
    Result initialize_render_extraction_pipeline();

    /// @brief 最小描画確認用の cube mesh / camera を GameWorld に追加する
    Result initialize_test_scene();

    /// @brief GameWorld の描画対象を frame resource に反映する
    Result update_draw_scene(uint32_t a_bufferIndex);

    /// @brief リサイズの適用
    /// @return
    Result apply_pending_resize();

    private:
    std::unique_ptr<FrameController> m_frameController = nullptr; // フレームコントローラー
    PAL::IPlatform* m_platform = nullptr;                         // プラットフォームインターフェースの非所有ポインタ
    Core::CQRS::Bridge* m_platformCommandBridge = nullptr; // プラットフォームからコマンドを受け取るためのブリッジ
    PAL::PlatformRuntimeState m_platformRuntimeState;      // プラットフォームランタイム状態
    RHI::IRenderBackend* m_renderBackend = nullptr;        // レンダーバックエンドの非所有ポインタ

    std::unique_ptr<RHI::FrameGraph> m_frameGraph = nullptr;
    std::unique_ptr<RHI::FrameGraph> m_debugFrameGraph = nullptr;
    std::unique_ptr<RHI::FrameGraph> m_presentFrameGraph = nullptr;

    // --- 全体共有リソース ---
    RHI::RenderTargetResources m_finalColorRenderTarget{};
    RHI::RenderTargetResources m_debugColorRenderTarget{};

    // --- DrawSystem ---
    GameCore::GameWorld m_gameWorld{};
    std::vector<DrawSystem::DrawScene> m_drawScenes{};
    std::vector<DrawSystem::DrawScene> m_debugDrawScenes{};
    DrawSystem::DrawFrameState m_drawFrameState{};
    DrawSystem::DrawFrameState m_debugDrawFrameState{};
    std::unique_ptr<DrawSystem::DrawResources> m_drawResources = nullptr;
    std::unique_ptr<DrawSystem::DrawResources> m_debugDrawResources = nullptr;
    std::unique_ptr<DrawSystem::MeshPool> m_meshPool = nullptr;
    ECS::ECSManager::SystemPipeline m_renderExtractionPipeline{};
    const DrawSystem::RenderView* m_debugRenderView = nullptr;
    bool m_isDebugRenderingEnabled = false;
    DrawSystem::RenderView m_renderViewOverride{};
    bool m_hasRenderViewOverride = false;

    // --- サブシステム ---
    uint32_t m_bufferCount = 1;
    uint32_t m_maxObjectCount = 0;
    uint32_t m_maxCellCount = 0;

    // --- 定数 ---
    const uint32_t k_maxObjectCount = 50000;
    const uint32_t k_cellObjectCapacity = 256;
};
} // namespace Cue
