#include <memory>
#include <cstdint>

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
    imguiSetupInfo.commandQueue = d3d12Backend->get_graphics_command_queue();
    imguiSetupInfo.rtvFormat = d3d12Backend->get_rtv_format();
    Cue::GraphicsCore::DX12::font_srv_for_imgui fontSrvInfo = d3d12Backend->get_font_srv_for_imgui();
    imguiSetupInfo.srvDescHeap = fontSrvInfo.srvDescHeap;
    imguiSetupInfo.fontSrvCpuDescHandle = fontSrvInfo.cpuDescHandle;
    imguiSetupInfo.fontSrvGpuDescHandle = fontSrvInfo.gpuDescHandle;
    imguiManager.initialize(imguiSetupInfo);
    const uint64_t imguiMessageHandlerId = win->register_message_handler(
        [](Cue::Platform::Win::NativeWindowHandle hwnd, uint32_t msg, std::uintptr_t wParam, std::intptr_t lParam, std::intptr_t& outResult)
        {
            const bool handled = ImGui_ImplWin32_WndProcHandler(
                reinterpret_cast<HWND>(hwnd),
                static_cast<UINT>(msg),
                static_cast<WPARAM>(wParam),
                static_cast<LPARAM>(lParam));
            if (handled)
            {
                outResult = 1;
                return true;
            }

            return false;
        });
    Cue::GraphicsCore::FrameGraph* presentFrameGraph = d3d12Backend->get_present_frame_graph();
    if (presentFrameGraph != nullptr)
    {
        auto pass = presentFrameGraph->add_pass<Cue::Editor::ImGuiPass>(imguiManager);
        pass;
    }
    Cue::Result r = presentFrameGraph->build();
    r;

    // 5) メインループ
    bool isRunning = true;
    while (isRunning)
    {
        // 5-1) メッセージの処理
        isRunning = win->poll_message();

        // 5-1.5) ImGuiのフレーム開始
        if (imguiManager.begin_frame())
        {
            ImGui::Begin("Hello, ImGui!"); // ウィンドウを作成
            ImGui::Text("This is a simple text in the ImGui window."); // テキストを表示
            ImGui::End();
            imguiManager.end_frame();
        }

        // 5-2) エンジンの更新と描画
        engine.tick();
    }

    if (imguiMessageHandlerId != 0)
    {
        (void)win->unregister_message_handler(imguiMessageHandlerId);
    }

    // 6) Editorのシャットダウン
    imguiManager.shutdown();

    // 7) エンジンのシャットダウン
    engine.shutdown();

    // 8) アプリケーションの終了
    return 0;
}
