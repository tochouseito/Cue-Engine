#pragma once

// === Core includes ===
#include <CQRS/CQRS.h>

// === PAL includes ===
#include <PAL.h>
#include <PlatformRuntimeState.h>

// === RHI includes ===
#include <FrameGraph.h>
#include <RHI.h>

// === Engine includes ===
#include "Asset/AssetManager.h"
#include "FrameController.h"
#include "Commands.h"
#include "GameCore/GameCore.h"

// === C++ includes ===
#include <functional>
#include <memory>
#include <vector>

namespace Cue
{
    class EngineCommandContext final : public IGameCommandContext
    {
    public:
        explicit EngineCommandContext(GameCore& a_gameCore) noexcept
            : m_gameCore(a_gameCore)
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

    private:
        GameCore& m_gameCore;
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

    private:
        Result create_final_color_resources();
        Result destroy_final_color_resources();
        Result create_frame_graphs(std::unique_ptr<RHI::FrameGraphPass> a_editorPass);
        Result destroy_size_dependent_resources();
        Result apply_pending_resize();

        /// @brief 更新
        std::function<void(uint64_t, uint32_t)> update();
        /// @brief 描画
        std::function<void(uint64_t, uint32_t)> render();
        /// @brief present
        std::function<void(uint64_t, uint32_t)> present();

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
        PAL::PlatformRuntimeState m_platformRuntimeState{};
        RHI::TextureHandle m_finalColorHandle{};
        RHI::ViewHandle m_finalColorRtvHandle{};
        RHI::ViewHandle m_finalColorSrvHandle{};
        uint32_t m_cubeIndexCount = 0;
    };
} // namespace Cue
