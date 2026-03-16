// === Base include ===
#include <Result.h>
#include <CueAssert.h>

// === Core include ===
#include <Logger.h>

// === Platform include ===
#include <win_platform.h>

// === Windows include ===
#define WIN32_LEAN_AND_MEAN             // windows ヘッダー軽量化
#define NOMINMAX                        // min と max マクロ抑止
#include <Windows.h>

// === GraphicsBackend include ===
#include <d3d12_backend.h>

// === Engine include ===
#include <Engine.h>

// === Editor include ===
#include "ImGuiManager.h"
#include "Statistics.h"
#include "DebugView.h"

// === C++ include ===
#include <memory>
#include <cstdint>
#include <deque>

// === ImGui include ===
#include <imgui.h>

// windows アプリのエントリーポイント
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    // 1) 各パラメータ
    uint32_t screenWidth = 1280;
    uint32_t screenHeight = 720;
    uint32_t bufferCount = 3;

    // 2) プラットフォームとグラフィックスバックエンドの作成
    auto win = std::make_unique<Cue::Platform::Win::WinPlatform>();
    auto d3d12Backend = std::make_unique<Cue::GraphicsCore::DX12::D3D12Backend>();

    // 3) win platform を d3d12 backend へ設定
    d3d12Backend->set_win_platform(win.get());

    // 4) win platform 初期化
    win->setup();

    // 5) d3d12 backend 初期化
    Cue::GraphicsCore::backend_setup_info backendSetupInfo{};
    backendSetupInfo.bufferCount = 3;
    backendSetupInfo.screenWidth = win->window_width();
    backendSetupInfo.screenHeight = win->window_height();
    backendSetupInfo.transformBufferPoolDesc.slotCapacity = 2;
    d3d12Backend->initialize(backendSetupInfo);

    // 6) editor 初期化

    // 6-1) ImGuiManager 初期化設定を構築
    Cue::Editor::imgui_setup_info imguiSetupInfo;
    imguiSetupInfo.hwnd = static_cast<HWND>(win->get_native_window_handle());   // ImGui_ImplWin32_WndProcHandler へ渡すために HWND にキャスト
    imguiSetupInfo.device = d3d12Backend->get_device();                         // ImGui_ImplDX12_Init へ渡すために ID3D12Device* を取得
    imguiSetupInfo.commandQueue = d3d12Backend->get_graphics_command_queue();   // ImGui_ImplDX12_Init へ渡すために ID3D12CommandQueue* を取得
    imguiSetupInfo.rtvFormat = d3d12Backend->get_rtv_format();                  // ImGui_ImplDX12_Init へ渡すために RTV のフォーマットを取得
    Cue::GraphicsCore::DX12::font_srv_for_imgui fontSrvInfo = d3d12Backend->get_font_srv_for_imgui();// ImGui_ImplDX12_Init へ渡すために フォント用の SRV 情報を取得
    imguiSetupInfo.srvDescHeap = fontSrvInfo.srvDescHeap;                       // ImGui_ImplDX12_Init へ渡すために SRV デスクリプタヒープを取得
    imguiSetupInfo.fontSrvCpuDescHandle = fontSrvInfo.cpuDescHandle;            // ImGui_ImplDX12_Init へ渡すために フォント用 SRV の CPU デスクリプタハンドルを取得
    imguiSetupInfo.fontSrvGpuDescHandle = fontSrvInfo.gpuDescHandle;            // ImGui_ImplDX12_Init へ渡すために フォント用 SRV の GPU デスクリプタハンドルを取得

    // 6-2) ImGuiManager を初期化
    Cue::Editor::ImGuiManager imguiManager;
    imguiManager.initialize(imguiSetupInfo);

    // 6-3) ImGuiMessageHandler を登録
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


    // 7) エンジンの初期化

    // 7-1) 初期化情報構築
    Cue::EngineInitInfo initInfo;
    initInfo.platform = win.get();
    initInfo.graphicsBackend = d3d12Backend.get();

    // 7-2) editor pass として ImGuiPass を登録
    std::unique_ptr<Cue::Editor::ImGuiPass> imguiPass = std::make_unique<Cue::Editor::ImGuiPass>(imguiManager);
    initInfo.editorPass = std::move(imguiPass);

    Cue::Engine engine;
    bool isRunning = engine.initialize(initInfo);

    // 7-3) debug view 作成
    Cue::Editor::DebugView debugView(*win, engine);

    // 7-4) statistics 作成
    Cue::Editor::Statistics statistics(engine.frame_controller());

    // 8) メインループ
    while (isRunning)
    {
        // 8-1) Windows のメッセージ処理
        isRunning = win->poll_message();

        // 8-2) imgui フレーム開始
        if (imguiManager.begin_frame())
        {
            ImGui::Begin("Hello, ImGui!"); // ウィンドウ開始
            ImGui::Text("This is a simple text in the ImGui window."); // テキスト表示

            debugView.update(); // debug view 更新
            statistics.update();

            ImGui::End();
            imguiManager.end_frame();
        }

        // 8-2) エンジンの更新と描画
        engine.tick();
    }

    if (imguiMessageHandlerId != 0)
    {
        (void)win->unregister_message_handler(imguiMessageHandlerId);
    }

    // 9) editor シャットダウン
    imguiManager.shutdown();

    // 10) エンジンのシャットダウン
    engine.shutdown();

    // 11) グラフィックスバックエンドのシャットダウン
    d3d12Backend->shutdown();

    // 12) プラットフォームのシャットダウン
    win->shutdown();

    // 13) アプリケーションの終了
    return 0;
}
