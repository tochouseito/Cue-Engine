// === Base includes ===
#include <CueAssert.h>
#include <CueResult.h>

// === Core includes ===
#include <CQRS/CQRS.h>
#include <DebugTool/PerformanceCounter.h>
#include <IO/Logger.h>
#include <Time/FrameCounter.h>

// === WinPlatform includes ===
#include <Dialog/WinDialogService.h>
#include <win_platform.h>

// === D3D12Backend includes ===
#include <D3D12Backend.h>

// === Editor includes ===
#include "DebugCamera.h"
#include "EditorManager.h"
#include "EditorMcpBridge.h"
#include "ImGuiManager/ImGuiManager.h"

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
#include <string_view>
#include <vector>

using namespace Cue;

namespace
{
    [[nodiscard]] std::string get_project_path_argument(const char* a_commandLine)
    {
        if (a_commandLine == nullptr)
        {
            return {};
        }

        constexpr std::string_view prefix = "--project=";
        const std::string commandLine(a_commandLine);
        const size_t argumentStart = commandLine.find(prefix);
        if (argumentStart == std::string::npos)
        {
            return {};
        }

        const size_t valueStart = argumentStart + prefix.size();
        const size_t valueEnd = commandLine.find_first_of(" \t", valueStart);
        return commandLine.substr(valueStart, valueEnd - valueStart);
    }

    void apply_mcp_playback_request(
        Cue::Engine& a_engine,
        Cue::Editor::EditorMcpPlaybackRequest a_request) noexcept
    {
        Cue::Result result = Cue::Result::ok();
        switch (a_request)
        {
        case Cue::Editor::EditorMcpPlaybackRequest::play:
            result = a_engine.is_play_paused()
                         ? a_engine.request_resume_play()
                         : a_engine.request_start_play();
            break;
        case Cue::Editor::EditorMcpPlaybackRequest::pause:
            result = a_engine.request_pause_play();
            break;
        case Cue::Editor::EditorMcpPlaybackRequest::step:
            result = a_engine.request_step_play();
            break;
        case Cue::Editor::EditorMcpPlaybackRequest::none:
        default:
            return;
        }

        if (!result)
        {
            Cue::Core::IO::log(
                Cue::Core::IO::LogSink::console | Cue::Core::IO::LogSink::file,
                "CueEditor MCP playback request failed: {}", result.message);
        }
    }

    [[nodiscard]] Cue::Editor::EditorMcpPlaybackState get_mcp_playback_state(
        const Cue::Engine& a_engine) noexcept
    {
        if (a_engine.is_play_paused())
        {
            return Cue::Editor::EditorMcpPlaybackState::paused;
        }
        return a_engine.is_playing()
                   ? Cue::Editor::EditorMcpPlaybackState::playing
                   : Cue::Editor::EditorMcpPlaybackState::editing;
    }

    void apply_mcp_script_open_request(
        Cue::Editor::EditorManager& a_editorManager,
        Cue::Editor::EditorMcpBridge& a_bridge) noexcept
    {
        std::string assetPath{};
        if (!a_bridge.consume_script_open_request(assetPath))
        {
            return;
        }

        const Cue::Result result =
            a_editorManager.open_script_asset_in_visual_studio(Cue::Core::IO::Path(assetPath));
        a_bridge.set_script_open_state(
            result ? Cue::Editor::EditorMcpScriptOpenState::succeeded
                   : Cue::Editor::EditorMcpScriptOpenState::failed);
        if (!result)
        {
            Cue::Core::IO::log(
                Cue::Core::IO::LogSink::console | Cue::Core::IO::LogSink::file,
                "CueEditor MCP script open request failed: {}", result.message);
        }
    }
} // namespace

// windows アプリのエントリーポイント
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR a_commandLine, int)
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
    std::unique_ptr<Core::CQRS::Bridge> gameBridge = std::make_unique<Core::CQRS::Bridge>();
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

    // OS 標準 UI は Platform 本体から分離し、Editor が必要なサービスだけを受け取る
    std::unique_ptr<PAL::Win::WinDialogService> dialogService =
        std::make_unique<PAL::Win::WinDialogService>(platform->get_window_handle());

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

    // DebugCamera は Engine の DebugFrameGraph が参照するため、Engine 初期化前に用意する。
    std::unique_ptr<Editor::DebugCamera> debugCamera = std::make_unique<Editor::DebugCamera>();

    // エンジンを初期化
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    EngineSetupInfo engineSetupInfo{}; // エンジンのセットアップ情報
    engineSetupInfo.platform = platform.get();
    engineSetupInfo.platformCommandBridge = platformBridge.get();
    engineSetupInfo.gameCommandBridge = gameBridge.get();
    engineSetupInfo.renderBackend = renderBackend.get();
    engineSetupInfo.maxFps = maxFps;
    engineSetupInfo.editorPass = std::make_unique<Editor::ImGuiPass>(*imGuiManager);
    engineSetupInfo.debugRenderView = &debugCamera->render_view();
    r = engine->initialize(engineSetupInfo);

    // 失敗したらログを出力して終了
    if (!r)
    {
        Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file, "Failed to initialize engine: %s",
                      r.message.data());
        CUE_ASSERT_FORMAT(false, "Failed to initialize engine: %s", r.message.data());
        return -1;
    }

    // Editor UI の所有と更新順は EditorManager に集約する。
    std::unique_ptr<Editor::EditorManager> editorManager = std::make_unique<Editor::EditorManager>();
    Editor::EditorManagerSetupInfo editorManagerSetupInfo{};
    editorManagerSetupInfo.backend = renderBackend.get();
    editorManagerSetupInfo.engine = engine.get();
    editorManagerSetupInfo.debugCamera = debugCamera.get();
    editorManagerSetupInfo.dialogService = dialogService.get();
    editorManagerSetupInfo.fileSystem = &platform->file_system();
    editorManagerSetupInfo.gameCommandBridge = gameBridge.get();
    editorManager->initialize(editorManagerSetupInfo);

    const std::string projectPath = get_project_path_argument(a_commandLine);
    if (!projectPath.empty())
    {
        editorManager->request_open_project(Core::IO::Path(projectPath));
    }

    std::unique_ptr<Editor::EditorMcpBridge> mcpBridge =
        std::make_unique<Editor::EditorMcpBridge>();
    r = mcpBridge->start({});
    if (!r)
    {
        Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                      "CueEditor MCP bridge startup failed: %s", r.message.data());
    }

    // WM_CLOSE は Platform の終了確定前に Editor へ渡し、未保存 Scene の確認を完了させる。
    platform->set_message_handler(
        [&editorManager](HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT& outResult) -> bool
        {
            if (message == WM_CLOSE && editorManager->request_exit())
            {
                outResult = 0;
                return true;
            }

            outResult = ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam);
            return outResult != 0;
        });

    // ウィンドウを表示
    platform->start();

    // メインループ
    bool isRunning = true;
    while (isRunning)
    {
        apply_mcp_playback_request(*engine, mcpBridge->consume_playback_request());
        apply_mcp_script_open_request(*editorManager, *mcpBridge);
        mcpBridge->set_playback_state(get_mcp_playback_state(*engine));

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
        bool shouldExit = false;

        if (r)
        {
#pragma region Editor UI の描画
            /// <summary>
            /// Editor UI の描画
            /// </summary>

            editorManager->update();

            shouldExit = editorManager->consume_exit_request();

#pragma endregion Editor UI の描画

            imGuiManager->end_frame();
        }

        if (shouldExit)
        {
            break;
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
        mcpBridge->set_playback_state(get_mcp_playback_state(*engine));

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
    mcpBridge->shutdown();
    mcpBridge.reset();
    imGuiManager->shutdown();
    imGuiManager.reset();
    engine->shutdown();
    engine.reset();
    dialogService.reset();
    renderBackend->shutdown();
    renderBackend.reset();
    platform->shutdown();
    platform.reset();

    return 0;
}
