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
        // 初期化情報をここに追加
        Platform::IPlatform* platform = nullptr;
        GraphicsCore::Backend* graphicsBackend = nullptr;

        std::unique_ptr<GraphicsCore::FrameGraphPass> editorPass = nullptr; // オプション：エディタ用のパスを受け取る
    };

    class Engine
    {
    public:
        Engine();
        ~Engine();
        bool initialize(EngineInitInfo& initInfo);
        void tick();
        void shutdown();
        Result submit_editor_command(std::unique_ptr<CQRS::ICommand> command);
        Result execute_editor_query(const CQRS::IQuery& query, CQRS::IQueryResult& outResult) const;

        FrameController& frame_controller() noexcept
        {
            return *m_frameController;
        }
    private:
        /// @brief 更新処理
        std::function<void(uint64_t, uint32_t)> update();
        /// @brief 描画処理
        std::function<void(uint64_t, uint32_t)> render();
        /// @brief フリップ処理
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
} // namespace Cue
