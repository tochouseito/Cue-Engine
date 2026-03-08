#include "Engine.h"
#include "pass/TestDraw.h"

// === Core includes ===
#include <Logger.h>
#include <IO/IFileSystem.h>

// === C++ Standard Library ===
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
    void Engine::initialize(EngineInitInfo& initInfo)
    {
        Result r = Result::ok();

        m_platform = initInfo.platform;
        m_graphicsBackend = initInfo.graphicsBackend;

        // 1) Platform の初期化と関連リソースの取得を行う

        // 2) EngineConfig の読み込み

        // 3) GraphicsBackend をセットアップする

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

        m_graphicsBackend->create_frame_graph(m_frameGraph);
        m_graphicsBackend->create_frame_graph(m_presentFrameGraph);
        if (initInfo.editorPass)
        {
            r = m_presentFrameGraph->add_pass(std::move(initInfo.editorPass));
        }
        else
        {
            // 4) Editor pass 未指定時でも描画経路を検証できるよう、最小の三角形描画 pass を既定登録する。
            r = m_presentFrameGraph->add_pass(std::make_unique<GraphicsCore::Pass::TestDrawPass>());
        }
        r = m_frameGraph->build();
        r = m_presentFrameGraph->build();

        m_platform->start();

        Core::Logger::log(Core::LogSink::debugConsole, "Engine initialized successfully.");
    }
    void Engine::tick()
    {
        m_platform->begin_frame();

        m_frameController->step();
        double fps = m_frameController->frame_counter().fps();
        uint32_t updateIndex = m_frameController->update_index();
        uint32_t renderIndex = m_frameController->render_index();
        uint32_t presentIndex = m_frameController->present_index();
        uint64_t totalFrame = m_frameController->total_frame();
        Core::Logger::log(Core::LogSink::debugConsole, "Frame: {}, FPS: {:.2f}, UpdateIndex: {}, RenderIndex: {}, PresentIndex: {}",
            totalFrame, fps, updateIndex, renderIndex, presentIndex);

        m_platform->end_frame();
    }
    void Engine::shutdown()
    {
        m_frameController.reset();

        // 設定ファイルの保存

        m_frameGraph.reset();
        m_presentFrameGraph.reset();
        m_graphicsBackend->shutdown();
        m_platform->shutdown();
        Core::Logger::log(Core::LogSink::debugConsole, "Engine shutdown completed.");
    }
    std::function<void(uint64_t, uint32_t)> Engine::update()
    {
        // 1) 更新処理のエントリポイントを返す
        // 2) 現在は仮実装でパイプライン入力を埋める
        return [this](uint64_t frameNo, uint32_t index)
            {
                (void)frameNo;
                (void)index;
                (void)this;
            };
    }
    std::function<void(uint64_t, uint32_t)> Engine::render()
    {
        // 2) 現在は仮実装でパイプライン入力を埋める
        return [this](uint64_t frameNo, uint32_t index)
            {
                (void)frameNo;
                (void)index;
                (void)this;
                m_graphicsBackend->render(frameNo, index, *m_frameGraph);
            };
    }
    std::function<void(uint64_t, uint32_t)> Engine::present()
    {
        // 1) Present 処理のエントリポイントを返す
        // 2) 現在は仮実装でパイプライン入力を埋める
        return [this](uint64_t frameNo, uint32_t index)
            {
                (void)this;
                m_graphicsBackend->present(frameNo, index, *m_presentFrameGraph);
            };
    }

} // namespace Cue
