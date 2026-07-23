#pragma once

/// ********************************************************************************
/// エンジン
/// ********************************************************************************

// === Base includes ===
#include <CueResult.h>

// === Core includes ===
#include <CQRS/CQRS.h>
#include <IO/Path.h>

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
#include "DrawSystem/DrawScene.h"
#include "DrawSystem/DrawSceneResources.h"
#include "DrawSystem/DrawViewResources.h"
#include "DrawSystem/DrawVisibilityResources.h"
#include "DrawSystem/MeshPool.h"
#include "DrawSystem/RenderCameraSelection.h"
#include "GameCore/GameWorld.h"
#include "Script/ScriptModule.h"
#include "Script/ScriptRuntime.h"
#include "Script/ScriptShadowCopyService.h"

// === C++ includes ===
#include <array>
#include <cstdint>
#include <memory>
#include <string>
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
    Core::CQRS::Bridge* gameCommandBridge = nullptr; // GameWorld 編集コマンドを受け取るためのブリッジ
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

    /// @brief Editor World の描画 Camera 選択を取得する。
    DrawSystem::RenderCameraSelection& game_render_camera_selection() noexcept
    {
        return m_gameRenderCameraSelection;
    }

    /// @brief Editor World を複製した runtime World の実行を開始する
    [[nodiscard]] Result request_start_play();

    /// @brief runtime World の更新を停止し、現在の描画状態を維持する
    [[nodiscard]] Result request_pause_play();

    /// @brief 停止中の runtime World 更新を再開する
    [[nodiscard]] Result request_resume_play();

    /// @brief 停止中の runtime World を 1 update だけ進める
    [[nodiscard]] Result request_step_play();

    /// @brief runtime World を破棄して Editor World の描画へ戻す
    [[nodiscard]] Result request_stop_play();

    /// @brief runtime World が生成されているかを返す
    [[nodiscard]] bool is_playing() const noexcept;

    /// @brief runtime World の更新が停止中かを返す
    [[nodiscard]] bool is_play_paused() const noexcept;

    DrawSystem::MeshPool* mesh_pool() noexcept
    {
        return m_meshPool.get();
    }

    /// @brief Project の Assets フォルダを設定する。
    void set_asset_root_path(const Core::IO::Path& a_assetRootPath) noexcept;

    /// @brief Project の Assets フォルダを返す。
    [[nodiscard]] const Core::IO::Path& asset_root_path() const noexcept
    {
        return m_assetRootPath;
    }

    /// @brief GameScript DLL を shadow copy からロードし、Editor と Play で共有する
    [[nodiscard]] Result load_script_module(
        const Core::IO::Path& a_scriptRoot,
        const Core::IO::Path& a_modulePath);

    /// @brief 現在の GameScript DLL を再ロードして class 一覧を更新する
    [[nodiscard]] Result reload_script_module();

    /// @brief ロード対象の GameScript DLL のパスを返す
    [[nodiscard]] const Core::IO::Path& script_module_path() const noexcept
    {
        return m_scriptModulePath;
    }

    /// @brief 現在ロード済みの GameScript class 名一覧を返す
    [[nodiscard]] const std::vector<std::string>& registered_script_classes() const noexcept
    {
        static const std::vector<std::string> k_emptyClasses{};
        return m_scriptModule != nullptr ? m_scriptModule->class_names() : k_emptyClasses;
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

    /// @brief 指定 GameWorld 用の DrawSystem 抽出 pipeline を構築する
    Result initialize_render_extraction_pipeline(
        GameCore::GameWorld& a_world,
        const DrawSystem::RenderCameraSelection& a_cameraSelection,
        ECS::ECSManager::SystemPipeline& a_outPipeline);

    /// @brief 初期描画に必要な Camera を GameWorld に追加する
    Result initialize_default_camera();

    /// @brief GameWorld の描画対象を frame resource に反映する
    Result update_draw_scene(uint32_t a_bufferIndex);

    /// @brief Editor UI から保留された Play 状態遷移を command drain 後に適用する
    Result apply_pending_play_request();

    /// @brief Editor World の保存可能な snapshot から runtime World を構築する
    Result start_play();

    /// @brief runtime World の実行状態を破棄する
    Result stop_play();

    /// @brief Script instance を破棄してロード済み DLL を解放する
    void unload_script_module() noexcept;

    /// @brief 現在描画する World を返す
    [[nodiscard]] GameCore::GameWorld& active_game_world() noexcept;

    /// @brief 現在描画する World に対応する抽出 pipeline を返す
    [[nodiscard]] ECS::ECSManager::SystemPipeline& active_render_extraction_pipeline() noexcept;

    /// @brief リサイズの適用
    /// @return
    Result apply_pending_resize();

    private:
    std::unique_ptr<FrameController> m_frameController = nullptr; // フレームコントローラー
    PAL::IPlatform* m_platform = nullptr;                         // プラットフォームインターフェースの非所有ポインタ
    Core::CQRS::Bridge* m_platformCommandBridge = nullptr; // プラットフォームからコマンドを受け取るためのブリッジ
    PAL::PlatformRuntimeState m_platformRuntimeState;      // プラットフォームランタイム状態
    RHI::IRenderBackend* m_renderBackend = nullptr;        // レンダーバックエンドの非所有ポインタ
    Core::CQRS::Bridge* m_gameCommandBridge = nullptr; // GameWorld 編集コマンドのブリッジ

    std::unique_ptr<RHI::FrameGraph> m_frameGraph = nullptr;
    std::unique_ptr<RHI::FrameGraph> m_debugFrameGraph = nullptr;
    std::unique_ptr<RHI::FrameGraph> m_presentFrameGraph = nullptr;

    // --- 全体共有リソース ---
    RHI::RenderTargetResources m_finalColorRenderTarget{};
    RHI::RenderTargetResources m_debugColorRenderTarget{};
    RHI::DepthStencilResources m_finalDepthStencil{};
    RHI::DepthStencilResources m_debugDepthStencil{};

    // --- DrawSystem ---
    enum class PlayState : uint8_t
    {
        editing,
        playing,
        paused,
    };

    enum class PlayRequest : uint8_t
    {
        none,
        start,
        pause,
        resume,
        step,
        stop,
    };

    GameCore::GameWorld m_gameWorld{};
    GameCore::GameWorld m_runtimeGameWorld{}; // Play 中の変更を authoring World へ戻さない複製先
    std::unique_ptr<Script::ScriptModule> m_scriptModule = nullptr;
    // 逆順破棄で ScriptRuntime が ScriptModule より先に破棄され、DLL 解放後の handle 参照を防ぐ
    Script::ScriptRuntime m_scriptRuntime{m_runtimeGameWorld};
    std::unique_ptr<Script::ScriptShadowCopyService> m_scriptShadowCopy = nullptr;
    DrawSystem::RenderCameraSelection m_gameRenderCameraSelection{};
    DrawSystem::RenderCameraSelection m_runtimeRenderCameraSelection{};
    std::vector<DrawSystem::DrawScene> m_drawScenes{};
    DrawSystem::DrawFrameState m_drawFrameState{};
    std::unique_ptr<DrawSystem::DrawSceneResources> m_drawSceneResources = nullptr;
    std::unique_ptr<DrawSystem::DrawViewResources> m_gameViewResources = nullptr;
    std::unique_ptr<DrawSystem::DrawViewResources> m_debugViewResources = nullptr;
    std::unique_ptr<DrawSystem::DrawVisibilityResources> m_gameVisibilityResources = nullptr;
    std::unique_ptr<DrawSystem::DrawVisibilityResources> m_debugVisibilityResources = nullptr;
    std::unique_ptr<DrawSystem::MeshPool> m_meshPool = nullptr;
    ECS::ECSManager::SystemPipeline m_renderExtractionPipeline{};
    ECS::ECSManager::SystemPipeline m_runtimeRenderExtractionPipeline{};
    const DrawSystem::RenderView* m_debugRenderView = nullptr;
    bool m_isDebugRenderingEnabled = false;
    DrawSystem::RenderView m_renderViewOverride{};
    bool m_hasRenderViewOverride = false;
    PlayState m_playState = PlayState::editing;
    PlayRequest m_pendingPlayRequest = PlayRequest::none;
    bool m_isPlayStepRequested = false;
    Core::IO::Path m_assetRootPath{};    // Project 由来の Assets フォルダ
    Core::IO::Path m_scriptRootPath{};   // shadow copy の配置基準になる GameScript root
    Core::IO::Path m_scriptModulePath{}; // 自動ビルド後に再ロードする GameScript DLL
    uint64_t m_scriptCopyId = 0u;

    // --- サブシステム ---
    uint32_t m_bufferCount = 1;
    uint32_t m_maxObjectCount = 0;
    uint32_t m_maxCellCount = 0;

    // --- 定数 ---
    const uint32_t k_maxObjectCount = 50000;
    const uint32_t k_cellObjectCapacity = 256;
};
} // namespace Cue
