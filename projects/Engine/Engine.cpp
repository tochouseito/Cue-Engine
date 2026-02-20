#include "Engine.h"
#include <Logger.h>

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
        m_platform = initInfo.platform;
        m_platform->setup();

        FrameControllerDesc frameControllerDesc{};
        frameControllerDesc.m_maxFps = 60;
        m_frameController = std::make_unique<FrameController>(
            frameControllerDesc,
            m_platform->get_thread_factory(),
            m_platform->get_clock(),
            m_platform->get_waiter(),
            update(),
            render(),
            present());

        m_platform->start();

        Core::Logger::log(Core::LogSink::debugConsole, "Engine initialized successfully.");
    }
    void Engine::tick()
    {
        m_platform->begin_frame();

        m_frameController->step();
        double fps = m_frameController->frame_counter().fps();
        Core::Logger::log(Core::LogSink::debugConsole, "Current FPS: {:.1f}", fps);

        m_platform->end_frame();
    }
    void Engine::shutdown()
    {
        m_frameController.reset();
        m_platform->shutdown();
        Core::Logger::log(Core::LogSink::debugConsole, "Engine shutdown completed.");
    }
    std::function<void(uint64_t, uint32_t)> Engine::update()
    {
        // 1) 更新処理のエントリポイントを返す
        // 2) 実装確定前でもパイプラインを動かすため仮実装にする
        return [this](uint64_t frameNo, uint32_t index)
            {
                (void)frameNo;
                (void)index;
                (void)this;
            };
    }
    std::function<void(uint64_t, uint32_t)> Engine::render()
    {
        // 2) 実装確定前でもパイプラインを動かすため仮実装にする
        return [this](uint64_t frameNo, uint32_t index)
            {
                (void)frameNo;
                (void)index;
                (void)this;
            };
    }
    std::function<void(uint64_t, uint32_t)> Engine::present()
    {
        // 1) Present 処理のエントリポイントを返す
        // 2) 実装確定前でもパイプラインを動かすため仮実装にする
        return [this](uint64_t frameNo, uint32_t index)
            {
                (void)frameNo;
                (void)index;
                (void)this;
            };
    }
} // namespace Cue
