#pragma once

// === Core includes ===
#include <CQRS/CQRS.h>

// === PAL includes ===
#include <PAL.h>
#include <PlatformCommands.h>

// === RHI includes ===
#include <FrameGraph.h>
#include <RHI.h>

// === Engine includes ===
#include "Asset/AssetManager.h"
#include "FrameController.h"
#include "Commands.h"
#include "GameCore/GameCore.h"

// === C++ includes ===
#include <vector>

namespace Cue
{
    class Engine;

    class EngineCommandContext final : public IGameCommandContext, public PAL::IPlatformCommandContext
    {
    public:
        EngineCommandContext(GameCore& a_gameCore, Engine& a_engine) noexcept
            : m_gameCore(a_gameCore)
            , m_engine(a_engine)
        {
        }

        Result add_object() override
        {
            return m_gameCore.add_object();
        }

        Result remove_object(uint32_t a_objectId) override
        {
            return m_gameCore.remove_object(a_objectId);
        }

        Result request_window_resize(uint32_t a_width, uint32_t a_height) override;

    private:
        GameCore& m_gameCore;
        Engine& m_engine;
    };

    /// @brief Engine 初期化時に必要な依存オブジェクトです。
    struct EngineSetupInfo final
    {
        PAL::IPlatform* platform = nullptr;
        RHI::IBackend* backend = nullptr;
        uint32_t maxFps = 60;

        std::unique_ptr<RHI::FrameGraphPass> editorPass = nullptr;
        Core::CQRS::Bridge* editorBridge = nullptr;
        Core::CQRS::Bridge* platformBridge = nullptr;
    };

    /// @brief Runtime 全体の統合窓口です。
    class Engine final
    {
        friend class EngineCommandContext;

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

        AssetManager& asset_manager() noexcept
        {
            return m_assetManager;
        }

        [[nodiscard]] bool has_pending_resize_request() const noexcept
        {
            return m_hasPendingResizeRequest;
        }

        [[nodiscard]] uint32_t pending_resize_width() const noexcept
        {
            return m_pendingResizeWidth;
        }

        [[nodiscard]] uint32_t pending_resize_height() const noexcept
        {
            return m_pendingResizeHeight;
        }

    private:
        /// @brief 更新
        std::function<void(uint64_t, uint32_t)> update();
        /// @brief 描画
        std::function<void(uint64_t, uint32_t)> render();
        /// @brief present
        std::function<void(uint64_t, uint32_t)> present();
        Result request_window_resize(uint32_t a_width, uint32_t a_height);

    private:
        PAL::IPlatform* m_platform = nullptr;
        RHI::IBackend* m_backend = nullptr;
        AssetManager m_assetManager{};
        std::unique_ptr<FrameController> m_frameController = nullptr;
        std::unique_ptr<RHI::FrameGraph> m_frameGraph = nullptr;
        std::unique_ptr<RHI::FrameGraph> m_presentFrameGraph = nullptr;
        std::unique_ptr<GameCore> m_gameCore = nullptr;
        Core::CQRS::Bridge* m_editorBridge = nullptr;
        Core::CQRS::Bridge* m_platformBridge = nullptr;
        uint32_t m_pendingResizeWidth = 0;
        uint32_t m_pendingResizeHeight = 0;
        bool m_hasPendingResizeRequest = false;
    };
} // namespace Cue
