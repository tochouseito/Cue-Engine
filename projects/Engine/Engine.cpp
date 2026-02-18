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
        m_platform->start();

        Core::Logger::log(Core::LogSink::debugConsole, "Engine initialized successfully.");
    }
    void Engine::tick()
    {
        m_platform->begin_frame();

        m_platform->end_frame();
    }
    void Engine::shutdown()
    {
    }
} // namespace Cue
