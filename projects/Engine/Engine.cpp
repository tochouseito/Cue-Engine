#include "Engine.h"
#include "pass/PresentToSwapChain.h"
#include "pass/TestDraw.h"

// core 関連 include
#include <Logger.h>
#include <IO/IFileSystem.h>

// c++ 標準ライブラリ include
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace Cue
{
    Engine::Engine()
    {
    }

    Engine::~Engine()
    {
    }
    bool Engine::initialize(EngineInitInfo& initInfo)
    {
        // 1) platform と backend 参照を保持して editor 向け context へ流す
        Result r = Result::ok();
        m_platform = initInfo.platform;
        m_graphicsBackend = initInfo.graphicsBackend;
        m_editorCommandContext.set_graphics_backend(m_graphicsBackend);
        m_editorQueryContext.set_graphics_backend(m_graphicsBackend);

        // 2) frame controller 初期設定を組み立てる
        FrameControllerDesc frameControllerDesc{};
        frameControllerDesc.m_bufferCount = 3;
        frameControllerDesc.m_maxFps = 120;
        frameControllerDesc.m_mode = m_engineConfig.m_mode;
        m_frameController = std::make_unique<FrameController>(
            frameControllerDesc,
            m_platform->get_thread_factory(),
            m_platform->get_clock(),
            m_platform->get_waiter(),
            update(),
            render(),
            present());

        // 3) test draw 用 mesh を static mesh pool へ登録する
        GraphicsCore::StaticMeshAllocationHandle cubeAllocationHandle{};
        {
            if (m_graphicsBackend == nullptr)
            {
                Core::Logger::log(Core::LogSink::debugConsole, "Graphics backend is null.");
                return false;
            }

            GraphicsCore::StaticMeshBufferPool* staticMeshBufferPool = m_graphicsBackend->get_static_mesh_buffer_pool();
            if (staticMeshBufferPool == nullptr)
            {
                Core::Logger::log(Core::LogSink::debugConsole, "StaticMeshBufferPool is not available.");
                return false;
            }

            Asset::ModelHandle cubeModelHandle{};
            r = m_assetManager.create_cube_model(cubeModelHandle);
            if (!r)
            {
                Core::Logger::log(Core::LogSink::debugConsole, "Failed to create cube model.");
                return false;
            }

            Core::Native::ModelData cubeModel{};
            r = m_assetManager.get_model(cubeModelHandle, cubeModel);
            if (!r)
            {
                Core::Logger::log(Core::LogSink::debugConsole, "Failed to resolve cube model data.");
                return false;
            }

            std::vector<GraphicsCore::StaticMeshAllocationHandle> allocations{};
            r = staticMeshBufferPool->upload_model(cubeModel, allocations);
            if (!r || allocations.empty())
            {
                Core::Logger::log(Core::LogSink::debugConsole, "Failed to upload cube model to static mesh pool.");
                return false;
            }

            cubeAllocationHandle = allocations.front();
        }

        GraphicsCore::TransformBufferPool* transformBufferPool = m_graphicsBackend->get_transform_buffer_pool();
        if (transformBufferPool == nullptr)
        {
            Core::Logger::log(Core::LogSink::debugConsole, "TransformBufferPool is not available.");
            return false;
        }

        // 4) GameCore を初期化し、update thread が書く transform slot と upload buffer を先に用意する。
        m_gameCore = std::make_unique<Cue::GameCore>();
        r = m_gameCore->initialize(*transformBufferPool);
        if (!r)
        {
            Core::Logger::log(Core::LogSink::debugConsole, "Failed to initialize GameCore transform slots.");
            return false;
        }

        // 5) render graph と present graph を生成する
        m_graphicsBackend->create_frame_graph(m_frameGraph);
        m_graphicsBackend->create_frame_graph(m_presentFrameGraph);

        // 6) pass 登録と graph build を行う
        r = m_frameGraph->add_pass(std::make_unique<GraphicsCore::Pass::TestDrawPass>(
            cubeAllocationHandle,
            *transformBufferPool,
            m_gameCore->front_cube_transform_slot(),
            m_gameCore->back_cube_transform_slot()));
        if (!r)
        {
            Core::Logger::log(Core::LogSink::debugConsole, "Failed to add TestDrawPass to frame graph.");
            return false;
        }

        if (initInfo.editorPass)
        {
            r = m_presentFrameGraph->add_pass(std::move(initInfo.editorPass));
            if (!r)
            {
                Core::Logger::log(Core::LogSink::debugConsole, "Failed to add editor pass to present frame graph.");
                return false;
            }
        }
        else
        {
            r = m_presentFrameGraph->add_pass(std::make_unique<GraphicsCore::Pass::PresentToSwapChainPass>());
            if (!r)
            {
                Core::Logger::log(Core::LogSink::debugConsole, "Failed to add PresentToSwapChainPass to present frame graph.");
                return false;
            }
        }

        r = m_frameGraph->build();
        if (!r)
        {
            Core::Logger::log(Core::LogSink::debugConsole, "Failed to build frame graph.");
            return false;
        }

        r = m_presentFrameGraph->build();
        if (!r)
        {
            Core::Logger::log(Core::LogSink::debugConsole, "Failed to build present frame graph.");
            return false;
        }

        // 7) platform を開始して実行可能状態へ入れる
        m_platform->start();

        Core::Logger::log(Core::LogSink::debugConsole, "Engine initialized successfully.");
        return true;
    }
    void Engine::tick()
    {
        m_platform->begin_frame();

        // 1) tick 先頭で editor command を排出
        const Result drainCommandsResult = m_editorBridge.drain_commands(m_editorCommandContext);
        if (!drainCommandsResult)
        {
            Core::Logger::log(Core::LogSink::debugConsole, drainCommandsResult.message);
        }

        // 2) frame controller を進めて統計を採取する
        m_frameController->step();
        double fps = m_frameController->frame_counter().fps();
        uint32_t updateIndex = m_frameController->update_index();
        uint32_t renderIndex = m_frameController->render_index();
        uint32_t presentIndex = m_frameController->present_index();
        uint64_t totalFrame = m_frameController->total_frame();
        Core::Logger::log(Core::LogSink::debugConsole, "Frame: {}, FPS: {:.2f}, UpdateIndex: {}, RenderIndex: {}, PresentIndex: {}",
            totalFrame, fps, updateIndex, renderIndex, presentIndex);

        // 3) platform 側フレーム終了処理を流す
        m_platform->end_frame();
    }
    void Engine::shutdown()
    {
        // 1) frame controller を停止する
        m_frameController.reset();

        // 2) graph 実体を解放する
        m_frameGraph.reset();
        m_presentFrameGraph.reset();

        // 3) 終了ログを出力する
        Core::Logger::log(Core::LogSink::debugConsole, "Engine shutdown completed.");
    }
    Result Engine::submit_editor_command(std::unique_ptr<CQRS::ICommand> command)
    {
        // 1) editor command 受付を engine へ集約
        return m_editorBridge.submit_command(std::move(command));
    }
    Result Engine::execute_editor_query(const CQRS::IQuery& query, CQRS::IQueryResult& outResult) const
    {
        // 1) editor query 実行 context を engine 所有へ固定
        return m_editorBridge.execute_query(query, m_editorQueryContext, outResult);
    }
    std::function<void(uint64_t, uint32_t)> Engine::update()
    {
        // 1) 更新コールバックを返し、GameCore の simulation を update thread から進める。
        return [this](uint64_t frameNo, uint32_t index)
            {
                // 1) frame index ごとの transform queue を更新し、render thread が別 slot を安全に読めるようにする。
                m_gameCore->update(frameNo, index);
            };
    }
    std::function<void(uint64_t, uint32_t)> Engine::render()
    {
        // 1) 仮実装 render コールバック返却
        return [this](uint64_t frameNo, uint32_t index)
            {
                // 1) render graph を実行する
                (void)frameNo;
                (void)index;
                (void)this;
                Result r = m_graphicsBackend->render(frameNo, index, *m_frameGraph);
                if (!r)
                {
                    Core::Logger::log(Core::LogSink::debugConsole, r.message);
                }
            };
    }
    std::function<void(uint64_t, uint32_t)> Engine::present()
    {
        // 1) present コールバック返却
        // 2) 仮実装で入力だけ受理
        return [this](uint64_t frameNo, uint32_t index)
            {
                // 1) present graph を実行する
                (void)this;
                Result r = m_graphicsBackend->present(frameNo, index, *m_presentFrameGraph);
                if (!r)
                {
                    Core::Logger::log(Core::LogSink::debugConsole, r.message);
                }
            };
    }

} // 名前空間 cue
