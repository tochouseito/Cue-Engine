#include <Windows.h>
#include <iostream>

// Platform
#include <win_platform.h>

// Engine
#include <Engine.h>

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    bool isRunning = true;
    Cue::Platform::Win::WinPlatform platform;
    platform.setup();
    platform.start();
    Cue::Engine engine;
    Cue::EngineInitInfo initInfo;
    initInfo.platform = &platform;
    engine.initialize(initInfo);
    while (isRunning)
    {
        isRunning = platform.poll_message();
        engine.tick();
    }

    engine.shutdown();

    return 0;
}
