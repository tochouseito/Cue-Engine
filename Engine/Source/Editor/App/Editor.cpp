// === Base includes ===
#include <CueAssert.h>
#include <CueResult.h>

// === Core includes ===
#include <CQRS/CQRS.h>
#include <DebugTool/PerformanceCounter.h>
#include <IO/Logger.h>
#include <Time/FrameCounter.h>

// === WinPlatform includes ===
#include <win_platform.h>

// === D3D12Backend includes ===
#include <D3D12Backend.h>

// === Editor includes ===
#include "DebugCamera.h"
#include "ImGuiManager/ImGuiManager.h"
#include "Workspace/DebugView.h"
#include "Workspace/Dockspace.h"
#include "Workspace/GameView.h"

// === Engine includes ===
#include <Engine.h>

// === ImGui includes ===
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

// === C++ includes ===
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace Cue;

// windows アプリのエントリーポイント
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    // パラメーター
    uint32_t width = 1280;
    uint32_t height = 720;
    const char* className = "CueEditorWindowClass";
    const char* title = "Cue Editor";
    uint32_t bufferCount = 3;
    bool enableDebugLayer = true;
    uint32_t maxFps = 60;
    Core::IO::Path logFilePath("logs/editor.log");

    // Windows プラットフォームの初期化
    std::unique_ptr<Core::CQRS::Bridge> platformBridge = std::make_unique<Core::CQRS::Bridge>();
    std::unique_ptr<PAL::Win::WinPlatform> platform = std::make_unique<PAL::Win::WinPlatform>();
    platform->set_command_bridge(platformBridge.get());
    PAL::PlatformSetupInfo platformSetupInfo{}; // プラットフォームのセットアップ情報
    platformSetupInfo.width = width;
    platformSetupInfo.height = height;
    platformSetupInfo.className = className;
    platformSetupInfo.title = title;
    Result r = platform->initialize(platformSetupInfo);

    // Logger にプラットフォームのファイルシステムをセット
    Core::IO::set_log_file(platform->file_system(), Core::IO::Path("logs/editor.log"), true);

    // 失敗したらログを出力して終了
    if (!r)
    {
        Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file, "Failed to initialize platform: %s",
                      r.message.data());
        CUE_ASSERT_FORMAT(false, "Failed to initialize platform: %s", r.message.data());
        return -1;
    }

    // PerformanceCounter を初期化
    Core::PerformanceCounter profiler(platform->clock());

    // レンダーバックエンドを初期化
    std::unique_ptr<RHI::DX12::D3D12Backend> renderBackend = std::make_unique<RHI::DX12::D3D12Backend>();
    RHI::RenderBackendSetupInfo renderBackendSetupInfo{};
    renderBackendSetupInfo.width = width;
    renderBackendSetupInfo.height = height;
    renderBackendSetupInfo.bufferCount = bufferCount;
    renderBackendSetupInfo.enableDebugLayer = enableDebugLayer;
    renderBackend->set_win_platform(platform.get()); // プラットフォームをセット
    r = renderBackend->initialize(renderBackendSetupInfo);

    // 失敗したらログを出力して終了
    if (!r)
    {
        Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file, "Failed to initialize render backend: %s",
                      r.message.data());
        CUE_ASSERT_FORMAT(false, "Failed to initialize render backend: %s", r.message.data());
        return -1;
    }

    // ImGui マネージャーの初期化
    std::unique_ptr<Editor::ImGuiManager> imGuiManager = nullptr;
    Cue::Editor::ImGuiSetupInfo imGuiInfo(renderBackend->buffer_count());
    imGuiInfo.hwnd = platform->get_window_handle();
    imGuiInfo.device = renderBackend->imgui_device();
    imGuiInfo.commandQueue = renderBackend->imgui_command_queue();
    imGuiInfo.srvDescHeap = renderBackend->imgui_srv_descriptor_heap();
    imGuiInfo.renderBackend = renderBackend.get();
    imGuiInfo.fileSystem = &platform->file_system();
    imGuiManager = std::make_unique<Editor::ImGuiManager>(imGuiInfo);

    // ImGuiMessageHandler を登録
    platform->set_message_handler(
        [](HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT& outResult) -> bool
        {
            outResult = ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam);
            return outResult != 0;
        });

    // エンジンを初期化
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    EngineSetupInfo engineSetupInfo{}; // エンジンのセットアップ情報
    engineSetupInfo.platform = platform.get();
    engineSetupInfo.platformCommandBridge = platformBridge.get();
    engineSetupInfo.renderBackend = renderBackend.get();
    engineSetupInfo.maxFps = maxFps;
    engineSetupInfo.editorPass = std::make_unique<Editor::ImGuiPass>(*imGuiManager);
    r = engine->initialize(engineSetupInfo);

    // 失敗したらログを出力して終了
    if (!r)
    {
        Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file, "Failed to initialize engine: %s",
                      r.message.data());
        CUE_ASSERT_FORMAT(false, "Failed to initialize engine: %s", r.message.data());
        return -1;
    }

    // Dockspace を初期化
    std::unique_ptr<Editor::Dockspace> dockspace = std::make_unique<Editor::Dockspace>();

    // 各ワークスペースを初期化
    std::unique_ptr<Editor::GameView> gameView = std::make_unique<Editor::GameView>(renderBackend.get());
    std::unique_ptr<Editor::DebugView> debugView = std::make_unique<Editor::DebugView>(renderBackend.get());
    std::unique_ptr<Editor::DebugCamera> debugCamera = std::make_unique<Editor::DebugCamera>();

    // ウィンドウを表示
    platform->start();

    // メインループ
    bool isRunning = true;
    while (isRunning)
    {
        // プラットフォームメッセージを処理
        PAL::PlatformMessage message = platform->poll_message();
        if (message == PAL::PlatformMessage::Quit)
        {
            break;
        }

#pragma region フレーム開始
        /// <summary>
        /// 各フレーム開始処理
        /// </summary>

        // ImGui マネージャー
        r = imGuiManager->begin_frame();

        if (r)
        {
#pragma region Editor UI の描画
            /// <summary>
            /// Editor UI の描画
            /// </summary>

            ImGui::Begin("Test");

            ImGui::Text("Test.txt");

            ImGui::End();

            dockspace->update();
            gameView->update();
            debugView->update();

            Editor::DebugCameraViewport debugCameraViewport{};
            debugCameraViewport.width = debugView->viewport_width();
            debugCameraViewport.height = debugView->viewport_height();
            debugCameraViewport.isHovered = debugView->is_viewport_hovered();
            debugCameraViewport.isFocused = debugView->is_focused();
            debugCamera->update(debugCameraViewport);
            if (debugCameraViewport.isHovered || debugCameraViewport.isFocused)
            {
                engine->set_render_view_override(debugCamera->render_view());
            }
            else
            {
                engine->clear_render_view_override();
            }

#pragma endregion Editor UI の描画

            imGuiManager->end_frame();
        }

        // プラットフォーム
        r = platform->begin_frame();

        // 失敗したらログを出力して終了
        if (!r)
        {
            Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file, "Failed to begin frame: %s",
                          r.message.data());
            CUE_ASSERT_FORMAT(false, "Failed to begin platform frame: %s", r.message.data());
            break;
        }

        // エンジンのフレーム処理
        r = engine->begin_frame();

        // 失敗したらログを出力して終了
        if (!r)
        {
            Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file, "Failed to begin engine frame: %s",
                          r.message.data());
            CUE_ASSERT_FORMAT(false, "Failed to begin engine frame: %s", r.message.data());
            break;
        }
#pragma endregion フレーム開始

        // エンジンのフレーム処理
        r = engine->tick();

        // 失敗したらログを出力して終了
        if (!r)
        {
            Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file, "Failed to tick engine: %s",
                          r.message.data());
            CUE_ASSERT_FORMAT(false, "Failed to tick engine: %s", r.message.data());
            break;
        }

        // エンジンのフレーム処理
        r = engine->end_frame();

        // 失敗したらログを出力して終了
        if (!r)
        {
            Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file, "Failed to end engine frame: %s",
                          r.message.data());
            CUE_ASSERT_FORMAT(false, "Failed to end engine frame: %s", r.message.data());
            break;
        }

        // フレーム終了
        r = platform->end_frame();

        // 失敗したらログを出力して終了
        if (!r)
        {
            Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file, "Failed to end frame: %s",
                          r.message.data());
            CUE_ASSERT_FORMAT(false, "Failed to end frame: %s", r.message.data());
            break;
        }
    }

    // クリーン
    imGuiManager->shutdown();
    imGuiManager.reset();
    engine->shutdown();
    engine.reset();
    renderBackend->shutdown();
    renderBackend.reset();
    platform->shutdown();
    platform.reset();

    return 0;
}
