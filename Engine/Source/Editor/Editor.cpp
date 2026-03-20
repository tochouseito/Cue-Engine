// === Base includes ===
#include <Result.h>
#include <CueAssert.h>

// === Windows includes ===
#include <win_platform.h>

// === d3d12_backend includes ===
#include <d3d12_backend.h>

// === Engine includes ===
#include <Engine.h>

using namespace Cue;

// windows アプリのエントリーポイント
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    // プラットフォームの作成
    auto platform = std::make_unique<PAL::Win::WinPlatform>();

    // プラットフォームの初期化
    PAL::platform_setup_info platformInfo{};
    platformInfo.width = 1280;
    platformInfo.height = 720;
    platformInfo.className = "CueEditorWindowClass";
    platformInfo.title = "Cue Editor";
    Result r = platform->initialize(platformInfo);
    if (!r)
    {
        CUE_ASSERTF(false, "Failed to initialize platform: %s (code: %s, severity: %s) at %s:%u in function %s",
            r.message.data(), to_string(r.code), to_string(r.severity),
            r.file, r.line, r.function);
    }

    // プラットフォームの開始
    r = platform->start();

    // メインループ
    bool isRunning = true;
    while (isRunning)
    {
        PAL::PlatformMessage msg = platform->poll_message();
        if (msg == PAL::PlatformMessage::Quit)
        {
            isRunning = false;
        }
    }

    // プラットフォームのシャットダウン
    platform->shutdown();

    return 0;
}
