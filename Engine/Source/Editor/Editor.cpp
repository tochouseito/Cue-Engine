// === Base includes ===
#include <CueAssert.h>
#include <Result.h>

// === Windows includes ===
#include <WinPlatform.h>

// === D3D12 includes ===
#include <D3D12Backend.h>

// === Engine includes ===
#include <Engine.h>
#include <Commands.h>

// === Editor includes ===
#include "ImGuiManager.h"
#include "EditorManager.h"
#include "ProjectHub.h"

using namespace Cue;

// windows アプリのエントリーポイント
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    // パラメーター
    uint32_t width = 1280;
    uint32_t height = 720;
    std::string className = "CueEditorWindowClass";
    std::string title = "Cue Editor";
    uint32_t bufferCount = 3;
    bool enableDebugLayer = true;
    uint32_t maxFps = 60;

    // 宣言
    Result r = Result::ok();
    std::unique_ptr<PAL::Win::WinPlatform> platform = nullptr;
    std::unique_ptr<RHI::DX12::D3D12Backend> backend = nullptr;
    std::unique_ptr<Editor::ImGuiManager> imGuiManager = nullptr;
    Core::CQRS::Bridge editorBridge{};
    Core::CQRS::Bridge platformBridge{};
    uint64_t imguiMessageHandlerId = 0;
    std::unique_ptr<Engine> engine = nullptr;
    std::unique_ptr<Editor::EditorManager> editorManager = nullptr;
    std::unique_ptr<Editor::ProjectHub> projectHub = nullptr;
    std::string projectPath = "";

    // プラットフォームの生成
    platform = std::make_unique<Cue::PAL::Win::WinPlatform>();
    platform->set_platform_bridge(&platformBridge);

    // プラットフォームの初期化設定
    Cue::PAL::PlatformSetupInfo platformInfo{};
    platformInfo.width = width;
    platformInfo.height = height;
    platformInfo.className = className.c_str();
    platformInfo.title = title.c_str();

    // プラットフォームの初期化
    r = platform->initialize(platformInfo);

    // 失敗
    if (!r)
    {
#ifdef CUE_DEBUG
        CUE_ASSERTF(false,
            "Failed to initialize platform: %s (code: %s, severity: %s) at "
            "%s:%u in function %s",
            r.message.data(), Cue::to_string(r.code),
            Cue::to_string(r.severity), r.file, r.line, r.function);
#else
        Core::IO::log(Core::IO::LogSink::debugConsole,
            "Failed to initialize platform: {} (code: {}, severity: {}) at "
            "{}:{} in function {}",
            r.message, Cue::to_string(r.code),
            Cue::to_string(r.severity), r.file, r.line, r.function);
#endif
        return -1;
    }

    // レンダリングバックエンドの生成
    backend = std::make_unique<Cue::RHI::DX12::D3D12Backend>();
    backend->set_win_platform(platform.get());

    // レンダリングバックエンドの設定
    Cue::RHI::BackendSetupInfo backendInfo{};
    backendInfo.enableDebugLayer = enableDebugLayer;
    backendInfo.width = width;
    backendInfo.height = height;
    backendInfo.bufferCount = bufferCount;

    // レンダリングバックエンドの初期化
    r = backend->initialize(backendInfo);

    // 失敗
    if (!r)
    {
#ifdef CUE_DEBUG
        CUE_ASSERTF(false,
            "Failed to initialize rendering backend: %s (code: %s, "
            "severity: %s) at %s:%u in function %s",
            r.message.data(), Cue::to_string(r.code),
            Cue::to_string(r.severity), r.file, r.line, r.function);
#else
        Core::IO::log(Core::IO::LogSink::debugConsole,
            "Failed to initialize rendering backend: {} (code: {}, severity: {}) at "
            "{}:{} in function {}",
            r.message, Cue::to_string(r.code),
            Cue::to_string(r.severity), r.file, r.line, r.function);
#endif
        return -1;
    }

    // ImGui マネージャの初期化
    Cue::Editor::ImGuiSetupInfo imGuiInfo(backend->buffer_count());
    imGuiInfo.hwnd = platform->get_window_handle();
    imGuiInfo.device = backend->get_device();
    imGuiInfo.commandQueue = backend->get_graphics_command_queue();
    Cue::RHI::DX12::ImGuiFontSRVInfo fontSrvInfo = backend->get_font_srv_for_imgui();
    imGuiInfo.srvDescHeap = fontSrvInfo.srvDescHeap;
    imGuiInfo.fontSrvCpuDescHandle = fontSrvInfo.cpuDescHandle;
    imGuiInfo.fontSrvGpuDescHandle = fontSrvInfo.gpuDescHandle;
    imGuiInfo.fileSystem = &platform->file_system();
    imGuiManager = std::make_unique<Editor::ImGuiManager>(imGuiInfo);

    // ImGuiMessageHandler を登録
    imguiMessageHandlerId = platform->register_message_handler(
        [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, std::intptr_t& outResult)
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

    // エンジンの生成
    engine = std::make_unique<Cue::Engine>();

    // エンジンの設定
    Cue::EngineSetupInfo engineInfo{};
    engineInfo.platform = platform.get();
    engineInfo.backend = backend.get();
    engineInfo.maxFps = maxFps;
    engineInfo.editorPass = std::make_unique<Editor::ImGuiPass>(*imGuiManager);
    engineInfo.editorBridge = &editorBridge;
    engineInfo.platformBridge = &platformBridge;

    // エンジンの初期化
    r = engine->initialize(engineInfo);

    // 失敗
    if (!r)
    {
#ifdef CUE_DEBUG
        CUE_ASSERTF(false,
            "Failed to initialize engine: %s (code: %s, severity: %s) at "
            "%s:%u in function %s",
            r.message.data(), Cue::to_string(r.code),
            Cue::to_string(r.severity), r.file, r.line, r.function);
#else
        Core::IO::log(Core::IO::LogSink::debugConsole,
            "Failed to initialize engine: {} (code: {}, severity: {}) at "
            "{}:{} in function {}",
            r.message, Cue::to_string(r.code),
            Cue::to_string(r.severity), r.file, r.line, r.function);
#endif
        return -1;
    }

    // エディタマネージャの生成と初期化
    editorManager = std::make_unique<Editor::EditorManager>(&editorBridge, platform.get(), backend.get(), engine.get());
    editorManager->initialize();

    // プロジェクトハブの生成と初期化
    projectHub = std::make_unique<Editor::ProjectHub>();

    // プラットフォームの開始
    r = platform->start();

    // メインループ
    bool isRunning = true;
    bool showProjectHub = projectPath.empty(); // プロジェクトパスが空の場合は Project Hub を表示
    while (isRunning)
    {
        // プラットフォームのメッセージを処理
        Cue::PAL::PlatformMessage msg = platform->poll_message();
        if (msg == Cue::PAL::PlatformMessage::Quit)
        {
            isRunning = false;
            break;
        }

        // ImGui マネージャのフレーム開始処理
        if (imGuiManager->begin_frame())
        {
            if(showProjectHub)
            {
                projectHub->update();
                showProjectHub = projectHub->is_open();
                projectPath = projectHub->project_path();
            }
            else
            {
                editorManager->update();
            }
            imGuiManager->end_frame();
        }

        // エンジンのフレーム開始処理
        r = engine->begin_frame();

        // エンジンのティック処理
        r = engine->tick();

        // エンジンのフレーム終了処理
        r = engine->end_frame();
    }

    // エンジンのシャットダウン
    engine->shutdown();
    engine.reset();

    // ImGui マネージャのシャットダウン
    imGuiManager->shutdown();
    imGuiManager.reset();

    // ImGui メッセージハンドラの解除
    platform->unregister_message_handler(imguiMessageHandlerId);

    // レンダリングバックエンドのシャットダウン
    backend->shutdown();
    backend.reset();

    // プラットフォームのシャットダウン
    platform->shutdown();
    platform.reset();

    return 0;
}
