#include <memory>

// Platform
#ifdef PLATFORM_WIN
#include <Windows.h>
#include <win_platform.h>
#endif

// Engine
#include <Engine.h>

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    bool isRunning = true;
    auto platform = Cue::Platform::create_platform();
    platform->setup();
    platform->start();
    Cue::Engine engine;
    Cue::EngineInitInfo initInfo;
    initInfo.platform = platform.get();
    engine.initialize(initInfo);
    while (isRunning)
    {
        isRunning = platform->poll_message();
        engine.tick();
    }

    engine.shutdown();

    return 0;
}
