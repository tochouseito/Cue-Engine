// === Base includes ===
#include <CueAssert.h>
#include <Result.h>

// === Core includes ===
#include <IO/Logger.h>
#include <Time/Timer.h>

// === Windows includes ===
#include <WinPlatform.h>

// === D3D12 includes ===
#include <D3D12Backend.h>

// === Audio includes ===
#include <AudioBackendFactory.h>

// === Physics includes ===
#include <JoltPhysicsSystem.h>

// === Engine includes ===
#include <Engine.h>
#include <Commands.h>

// === Editor includes ===
#include "ImGuiManager.h"
#include "EditorManager.h"
#include "EditorLoopMetrics.h"
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
    std::unique_ptr<Audio::IBackend> audioBackend = nullptr;
    std::unique_ptr<Physics::Jolt::JoltPhysicsSystem> physicsSystem = nullptr;
    std::unique_ptr<Editor::ImGuiManager> imGuiManager = nullptr;
    Core::CQRS::Bridge editorBridge{};
    Core::CQRS::Bridge platformBridge{};
    uint64_t imguiMessageHandlerId = 0;
    std::unique_ptr<Engine> engine = nullptr;
    std::unique_ptr<Editor::EditorManager> editorManager = nullptr;
    std::unique_ptr<Editor::ProjectHub> projectHub = nullptr;
    Editor::EditorLoopMetrics currentLoopMetrics{};
    Editor::EditorLoopMetrics lastCompletedLoopMetrics{};
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

    r = platform->set_drag_drop_enabled(true);
    if (!r)
    {
#ifdef CUE_DEBUG
        CUE_ASSERTF(false,
            "Failed to enable drag and drop: %s (code: %s, severity: %s) at "
            "%s:%u in function %s",
            r.message.data(), Cue::to_string(r.code),
            Cue::to_string(r.severity), r.file, r.line, r.function);
#else
        Core::IO::log(Core::IO::LogSink::debugConsole,
            "Failed to enable drag and drop: {} (code: {}, severity: {}) at "
            "{}:{} in function {}",
            r.message, Cue::to_string(r.code),
            Cue::to_string(r.severity), r.file, r.line, r.function);
#endif
        return -1;
    }

    // レンダリングバックエンドの生成
    backend = std::make_unique<Cue::RHI::DX12::D3D12Backend>();
    backend->set_win_platform(platform.get());
    audioBackend = Audio::create_backend();
    if (audioBackend == nullptr)
    {
        return -1;
    }

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

    r = audioBackend->initialize();
    if (!r)
    {
#ifdef CUE_DEBUG
        CUE_ASSERTF(false,
            "Failed to initialize audio backend: %s (code: %s, "
            "severity: %s) at %s:%u in function %s",
            r.message.data(), Cue::to_string(r.code),
            Cue::to_string(r.severity), r.file, r.line, r.function);
#else
        Core::IO::log(Core::IO::LogSink::debugConsole,
            "Failed to initialize audio backend: {} (code: {}, severity: {}) at "
            "{}:{} in function {}",
            r.message, Cue::to_string(r.code),
            Cue::to_string(r.severity), r.file, r.line, r.function);
#endif
        backend->shutdown();
        backend.reset();
        return -1;
    }

    physicsSystem = std::make_unique<Physics::Jolt::JoltPhysicsSystem>();
    Physics::PhysicsWorldDesc physicsWorldDesc{};
    r = physicsSystem->initialize(physicsWorldDesc);
    if (!r)
    {
#ifdef CUE_DEBUG
        CUE_ASSERTF(false,
            "Failed to initialize physics system: %s (code: %s, "
            "severity: %s) at %s:%u in function %s",
            r.message.data(), Cue::to_string(r.code),
            Cue::to_string(r.severity), r.file, r.line, r.function);
#else
        Core::IO::log(Core::IO::LogSink::debugConsole,
            "Failed to initialize physics system: {} (code: {}, severity: {}) at "
            "{}:{} in function {}",
            r.message, Cue::to_string(r.code),
            Cue::to_string(r.severity), r.file, r.line, r.function);
#endif
        physicsSystem->shutdown();
        physicsSystem.reset();
        audioBackend->shutdown();
        audioBackend.reset();
        backend->shutdown();
        backend.reset();
        return -1;
    }

    // ImGui マネージャの初期化
    Cue::Editor::ImGuiSetupInfo imGuiInfo(backend->buffer_count());
    imGuiInfo.hwnd = platform->get_window_handle();
    imGuiInfo.device = backend->get_device();
    imGuiInfo.commandQueue = backend->get_graphics_command_queue();
    imGuiInfo.srvDescHeap = backend->get_imgui_srv_descriptor_heap();
    imGuiInfo.backend = backend.get();
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
    engineInfo.audioBackend = audioBackend.get();
    engineInfo.physicsSystem = physicsSystem.get();
    engineInfo.maxFps = maxFps;
    engineInfo.editorPass = std::make_unique<Editor::ImGuiPass>(*imGuiManager);
    engineInfo.editorBridge = &editorBridge;
    engineInfo.platformBridge = &platformBridge;
#if defined(CUE_PROJECT_ROOT_PATH)
    engineInfo.errorTexturePath = Core::IO::Path::join(
        Core::IO::Path(std::string(CUE_PROJECT_ROOT_PATH)),
        Core::IO::Path("Engine/Textures/CueDummy.dds"));
#endif

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
        physicsSystem->shutdown();
        physicsSystem.reset();
        audioBackend->shutdown();
        audioBackend.reset();
        backend->shutdown();
        backend.reset();
        return -1;
    }

    // エディタマネージャの生成と初期化
    editorManager = std::make_unique<Editor::EditorManager>(
        &editorBridge, &platform->file_system(), platform.get(), backend.get(),
        engine.get());
    editorManager->initialize();
    editorManager->set_loop_metrics_source(&lastCompletedLoopMetrics);

    // プロジェクトハブの生成と初期化
    projectHub = std::make_unique<Editor::ProjectHub>(platform->file_system());

    // プラットフォームの開始
    r = platform->start();

    // メインループ
    bool isRunning = true;
    bool showProjectHub = projectPath.empty();
    while (isRunning)
    {
        currentLoopMetrics = Editor::EditorLoopMetrics{};
        Core::Time::Timer loopTimer(platform->clock());
        loopTimer.start();

        // プラットフォームのメッセージを処理
        Core::Time::Timer pollTimer(platform->clock());
        pollTimer.start();
        Cue::PAL::PlatformMessage msg = platform->poll_message();
        pollTimer.stop();
        currentLoopMetrics.pollMessageMs =
            pollTimer.elapsed_ticks().ms_f64();
        if (msg == Cue::PAL::PlatformMessage::Quit)
        {
            isRunning = false;
            break;
        }

        // ImGui マネージャのフレーム開始処理
        Core::Time::Timer imguiBeginTimer(platform->clock());
        imguiBeginTimer.start();
        const Result beginImguiResult = imGuiManager->begin_frame();
        imguiBeginTimer.stop();
        currentLoopMetrics.imguiBeginMs =
            imguiBeginTimer.elapsed_ticks().ms_f64();
        if (beginImguiResult)
        {
            currentLoopMetrics.didDrawImgui = true;
            if (showProjectHub)
            {
                Core::Time::Timer projectHubTimer(platform->clock());
                projectHubTimer.start();
                projectHub->update();
                const std::string createdProjectPath = projectHub->project_path();
                if (!projectHub->is_open() && !createdProjectPath.empty())
                {
                    const Result openResult =
                        editorManager->open_project(createdProjectPath);
                    if (openResult)
                    {
                        projectPath = createdProjectPath;
                        showProjectHub = false;
                    }
                    else
                    {
                        Core::IO::log(Core::IO::LogSink::debugConsole,
                            "Failed to open project: {} (code: {}, severity: {}) at {}:{} in function {}",
                            openResult.message, Cue::to_string(openResult.code),
                            Cue::to_string(openResult.severity), openResult.file,
                            openResult.line, openResult.function);
                        projectHub = std::make_unique<Editor::ProjectHub>(
                            platform->file_system());
                    }
                }
                projectHubTimer.stop();
                currentLoopMetrics.projectHubUpdateMs =
                    projectHubTimer.elapsed_ticks().ms_f64();
            }
            else
            {
                Core::Time::Timer editorUpdateTimer(platform->clock());
                editorUpdateTimer.start();
                editorManager->update();
                editorUpdateTimer.stop();
                currentLoopMetrics.editorUpdateMs =
                    editorUpdateTimer.elapsed_ticks().ms_f64();
            }
            Core::Time::Timer imguiEndTimer(platform->clock());
            imguiEndTimer.start();
            imGuiManager->end_frame();
            imguiEndTimer.stop();
            currentLoopMetrics.imguiEndMs =
                imguiEndTimer.elapsed_ticks().ms_f64();
        }

        // エンジンのフレーム開始処理
        Core::Time::Timer engineBeginTimer(platform->clock());
        engineBeginTimer.start();
        r = engine->begin_frame();
        engineBeginTimer.stop();
        currentLoopMetrics.engineBeginMs =
            engineBeginTimer.elapsed_ticks().ms_f64();

        // エンジンのティック処理
        Core::Time::Timer engineTickTimer(platform->clock());
        engineTickTimer.start();
        r = engine->tick();
        engineTickTimer.stop();
        currentLoopMetrics.engineTickMs =
            engineTickTimer.elapsed_ticks().ms_f64();

        // エンジンのフレーム終了処理
        Core::Time::Timer engineEndTimer(platform->clock());
        engineEndTimer.start();
        r = engine->end_frame();
        engineEndTimer.stop();
        currentLoopMetrics.engineEndMs =
            engineEndTimer.elapsed_ticks().ms_f64();

        loopTimer.stop();
        currentLoopMetrics.loopTotalMs =
            loopTimer.elapsed_ticks().ms_f64();
        lastCompletedLoopMetrics = currentLoopMetrics;
    }

    // エンジンのシャットダウン
    engine->shutdown();
    engine.reset();

    physicsSystem->shutdown();
    physicsSystem.reset();

    audioBackend->shutdown();
    audioBackend.reset();

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
