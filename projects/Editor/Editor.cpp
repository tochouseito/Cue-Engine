#include <memory>

// Platform
#ifdef PLATFORM_WIN
#include <Windows.h>
#include <win_platform.h>
#endif

// Graphics
#include <d3d12_backend.h>

// Engine
#include <Engine.h>

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    bool isRunning = true;
    auto platform = Cue::Platform::create_platform();
    platform->setup();
    platform->start();
    auto backend = Cue::GraphicsCore::create_backend();
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
