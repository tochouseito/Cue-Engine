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
    auto backend = Cue::GraphicsCore::create_backend();
#ifdef BACKEND_DX12
    // WinPlatformをD3D12Backendにセット
    auto d3d12Backend = dynamic_cast<Cue::GraphicsCore::DX12::D3D12Backend*>(backend.get());
    d3d12Backend->set_win_platform(platform.get());
#endif
    Cue::Engine engine;
    Cue::EngineInitInfo initInfo;
    initInfo.platform = platform.get();
    initInfo.graphicsBackend = backend.get();
    engine.initialize(initInfo);
    while (isRunning)
    {
        isRunning = platform->poll_message();
        engine.tick();
    }

    engine.shutdown();

    return 0;
}
