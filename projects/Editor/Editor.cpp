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

// Editor
#include "ImGuiManager.h"

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    // 1) プラットフォームとグラフィックスバックエンドの作成
    auto win = std::make_unique<Cue::Platform::Win::WinPlatform>();
    auto d3d12Backend = std::make_unique<Cue::GraphicsCore::DX12::D3D12Backend>();

    // 2) WinPlatformをD3D12Backendにセット
    d3d12Backend->set_win_platform(win.get());

    // 3) エンジンの初期化
    Cue::Engine engine;
    Cue::EngineInitInfo initInfo;
    initInfo.platform = win.get();
    initInfo.graphicsBackend = d3d12Backend.get();
    engine.initialize(initInfo);

    // 4) Editorの初期化
    Cue::Editor::ImGuiManager imguiManager;
    Cue::Editor::imgui_setup_info imguiSetupInfo;
    imguiSetupInfo.hwnd = static_cast<HWND>(win->get_native_window_handle());
    imguiSetupInfo.device = d3d12Backend->get_device();
    imguiSetupInfo.rtvFormat = d3d12Backend->get_rtv_format();
    Cue::GraphicsCore::DX12::font_srv_for_imgui fontSrvInfo = d3d12Backend->get_font_srv_for_imgui();
    imguiSetupInfo.srvDescHeap = fontSrvInfo.srvDescHeap;
    imguiSetupInfo.fontSrvCpuDescHandle = fontSrvInfo.cpuDescHandle;
    imguiSetupInfo.fontSrvGpuDescHandle = fontSrvInfo.gpuDescHandle;
    imguiManager.initialize(imguiSetupInfo);
    Cue::GraphicsCore::FrameGraph* frameGraph = d3d12Backend->get_frame_graph();
    frameGraph->add_pass<Cue::Editor::ImGuiPass>(imguiManager);

    // 5) メインループ
    bool isRunning = true;
    while (isRunning)
    {
        // 5-1) メッセージの処理
        isRunning = win->poll_message();

        // 5-2) エンジンの更新と描画
        engine.tick();
    }

    // 6) Editorのシャットダウン
    imguiManager.shutdown();

    // 7) エンジンのシャットダウン
    engine.shutdown();

    // 8) アプリケーションの終了
    return 0;
}
