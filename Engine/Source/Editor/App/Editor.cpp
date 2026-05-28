// === Base includes ===
#include <CueAssert.h>
#include <CueResult.h>

// === Core includes ===
#include <IO/Logger.h>
#include <Time/FrameCounter.h>

// === WinPlatform includes ===
#include <win_platform.h>

using namespace Cue;

// windows アプリのエントリーポイント
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    // パラメーター
    uint32_t width = 1280;
    uint32_t height = 720;
    const char* className = "CueEditorWindowClass";
    const char* title = "Cue Editor";

    // 処理結果
    Result r = Result::ok();

    // プラットフォーム実装を初期化
    std::unique_ptr<PAL::Win::WinPlatform> platform = std::make_unique<PAL::Win::WinPlatform>();
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

    // test
    Core::Time::FrameCounter frameCounter(platform->clock(), platform->waiter());
    frameCounter.set_max_fps(0);

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

        // --- ここで Engine 側の更新と描画処理を呼び出す ---
        Core::IO::log(Core::IO::LogSink::console, "FPS : {:.2f}", frameCounter.fps());

        // フレーム終了
        r = platform->end_frame();

        // 失敗したらエラーを表示して終了
        if (!r)
        {
            CUE_ASSERT_FORMAT(false, "Failed to end frame: %s", r.message.data());
            return -1;
        }

        frameCounter.tick();
    }

    Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file, "Editor shutdown");
    Core::IO::clear_log_file();

    // 終了処理
    r = platform->shutdown();
    platform.reset();

    return 0;
}
