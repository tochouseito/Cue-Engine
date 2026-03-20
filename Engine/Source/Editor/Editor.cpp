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

    // レンダリングバックエンドの作成
    auto backend = std::make_unique<RHI::DX12::D3D12Backend>();
    backend->set_win_platform(platform.get());

    // レンダリングバックエンドの初期化
    RHI::backend_setup_info backendInfo{};
    backendInfo.enableDebugLayer = true;
    backendInfo.width = platformInfo.width;
    backendInfo.height = platformInfo.height;
    r = backend->initialize(backendInfo);
    if (!r)
    {
        CUE_ASSERTF(false, "Failed to initialize rendering backend: %s (code: %s, severity: %s) at %s:%u in function %s",
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

    // レンダリングバックエンドのシャットダウン
    backend->shutdown();

    // プラットフォームのシャットダウン
    platform->shutdown();

    return 0;
}
