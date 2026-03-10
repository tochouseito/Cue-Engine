#pragma once
#include <Platform.h>
#include <GraphicsCore.h>
#include <memory>
#include <functional>

#include "Configuration.h"
#include "asset/AssetManager.h"
#include "cqrs/cqrs.h"
#include "cqrs/Commands.h"
#include "cqrs/Queries.h"

namespace Cue
{
    struct EngineInitInfo
    {
        // 初期化入力
        Platform::IPlatform* platform = nullptr;
        GraphicsCore::Backend* graphicsBackend = nullptr;

        std::unique_ptr<GraphicsCore::FrameGraphPass> editorPass = nullptr; // editor 用 pass 入力
    };

    class Engine
    {
    public:
        /// @brief 生成
        Engine();
        /// @brief 破棄
        ~Engine();
        /// @brief 初期化
        bool initialize(EngineInitInfo& initInfo);
        /// @brief 1 フレーム進行
        void tick();
        /// @brief 終了
        void shutdown();
        /// @brief editor command 受付
        Result submit_editor_command(std::unique_ptr<CQRS::ICommand> command);
        /// @brief editor query 実行
        Result execute_editor_query(const CQRS::IQuery& query, CQRS::IQueryResult& outResult) const;

        /// @brief frame controller 取得
        FrameController& frame_controller() noexcept
        {
            return *m_frameController;
        }
    private:
        /// @brief 更新
        std::function<void(uint64_t, uint32_t)> update();
        /// @brief 描画
        std::function<void(uint64_t, uint32_t)> render();
        /// @brief present
        std::function<void(uint64_t, uint32_t)> present();
    private:
        Platform::IPlatform* m_platform = nullptr;
        GraphicsCore::Backend* m_graphicsBackend = nullptr;
        Asset::AssetManager m_assetManager{};

        EngineConfig m_engineConfig{};

        std::unique_ptr<FrameController> m_frameController = nullptr;
        std::unique_ptr<GraphicsCore::FrameGraph> m_frameGraph = nullptr;
        std::unique_ptr<GraphicsCore::FrameGraph> m_presentFrameGraph = nullptr;
        CQRS::Bridge m_editorBridge{};
        CQRS::Commands::EngineCommandContext m_editorCommandContext{};
        CQRS::Queries::EngineQueryContext m_editorQueryContext{};
    };
} // 名前空間 cue
