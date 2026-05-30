// === Base includes ===
#include <CueAssert.h>
#include <CueResult.h>

// === Core includes ===
#include <IO/Logger.h>
#include <Time/FrameCounter.h>
#include <CQRS/CQRS.h>
#include <DebugTool/Profiler.h>

// === WinPlatform includes ===
#include <win_platform.h>

// === D3D12Backend includes ===
#include <D3D12Backend.h>

// === Engine includes ===
#include <Engine.h>

using namespace Cue;

// windows アプリのエントリーポイント
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    // パラメーター
    uint32_t width = 1280;
    uint32_t height = 720;
    const char* className = "CueEditorWindowClass";
    const char* title = "Cue Editor";
    uint32_t maxFps = 0;
    uint32_t bufferCount = 3;

    // 処理結果
    Result r = Result::ok();

    // プラットフォーム実装を初期化
    std::unique_ptr<PAL::Win::WinPlatform> platform = std::make_unique<PAL::Win::WinPlatform>();
    std::unique_ptr<Core::CQRS::Bridge> commandBridge = std::make_unique<Core::CQRS::Bridge>();
    platform->set_command_bridge(commandBridge.get()); // コマンドブリッジをプラットフォームにセット
    PAL::PlatformSetupInfo setupInfo{};
    setupInfo.width = width;
    setupInfo.height = height;
    setupInfo.className = className;
    setupInfo.title = title;
    r = platform->initialize(setupInfo);

    // 失敗したらエラーを表示して終了
    if (!r)
    {
        CUE_ASSERT_FORMAT(false, "Failed to initialize platform: %s", r.message.data());
        Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file, "Failed to initialize platform: %s", r.message.data());
        return -1;
    }

    // Logger にプラットフォームのファイルシステムをセット
    Core::IO::set_log_file(platform->file_system(), Core::IO::Path("logs/editor.log"), true);

    // Profiler を初期化
    Core::Profiler profiler(platform->clock());

    // レンダーバックエンドを初期化
    std::unique_ptr<RHI::DX12::D3D12Backend> renderBackend = std::make_unique<RHI::DX12::D3D12Backend>();
    RHI::RenderBackendSetupInfo renderBackendSetupInfo{};
    renderBackendSetupInfo.enableDebugLayer = true;
    renderBackendSetupInfo.width = width;
    renderBackendSetupInfo.height = height;
    renderBackendSetupInfo.bufferCount = bufferCount;
    r = renderBackend->initialize(renderBackendSetupInfo);

    // 失敗したらエラーを表示して終了
    if (!r)
    {
        CUE_ASSERT_FORMAT(false, "Failed to initialize render backend: %s", r.message.data());
        Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file, "Failed to initialize render backend: %s", r.message.data());
        return -1;
    }

    // Engine を初期化
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    EngineSetupInfo engineSetupInfo{};
    engineSetupInfo.maxFps = maxFps; // 最大フレームレートを Engine にセット
    engineSetupInfo.platform = platform.get(); // プラットフォームを Engine にセット
    engineSetupInfo.platformCommandBridge = commandBridge.get(); // コマンドブリッジを Engine にセット
    engineSetupInfo.renderBackend = renderBackend.get(); // レンダーバックエンドを Engine にセット
    engine->initialize(engineSetupInfo);

    // ウィンドウ表示を開始
    r = platform->start();

    // メインループ
    bool isRunning = true;
    while (isRunning)
    {
        // プラットフォームメッセージを処理
        PAL::PlatformMessage message = platform->poll_message();
        if (message == PAL::PlatformMessage::Quit)
        {
            isRunning = false;
        }

        // フレーム開始
        r = platform->begin_frame();

        // 失敗したらエラーを表示して終了
        if (!r)
        {
            CUE_ASSERT_FORMAT(false, "Failed to begin frame: %s", r.message.data());
            return -1;
        }

        r = engine->begin_frame();

        // 失敗したらエラーを表示して終了
        if (!r)
        {
            CUE_ASSERT_FORMAT(false, "Failed to begin engine frame: %s", r.message.data());
            return -1;
        }

        // --- ここで Engine 側の更新と描画処理を呼び出す ---
        r = engine->tick();

        // 失敗したらエラーを表示して終了
        if (!r)
        {
            CUE_ASSERT_FORMAT(false, "Failed to tick engine: %s", r.message.data());
            return -1;
        }

        const Core::Time::FrameCounter& frameCounter = engine->frame_controller().frame_counter();
        if (frameCounter.total_frames() > 0)
        {
            Core::IO::log(Core::IO::LogSink::console, "FPS : {:.2f}", frameCounter.fps());
        }
        /*profiler.begin("Test", "Update");
        profiler.end("Test", "Update");
        if (const auto snapshot = profiler.get_snapshot("Test", "Update"))
        {
            Core::IO::log(Core::IO::LogSink::console, "Update Time : {:.2f} ms", snapshot->timer.elapsed_seconds() * 1000.0);
        }*/

        r = engine->end_frame();

        // 失敗したらエラーを表示して終了
        if (!r)
        {
            CUE_ASSERT_FORMAT(false, "Failed to end engine frame: %s", r.message.data());
            return -1;
        }

        // フレーム終了
        r = platform->end_frame();

        // 失敗したらエラーを表示して終了
        if (!r)
        {
            CUE_ASSERT_FORMAT(false, "Failed to end frame: %s", r.message.data());
            return -1;
        }
    }

    Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file, "Editor shutdown");
    Core::IO::clear_log_file();

    // 終了処理
    r = platform->shutdown();
    platform.reset();

    return 0;
}
