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
    PAL::PlatformSetupInfo platformInfo{};
    platformInfo.width = 1280;
    platformInfo.height = 720;
    platformInfo.className = "CueEditorWindowClass";
    platformInfo.title = "Cue Editor";
    Cue::Result r = platform->initialize(platformInfo);
    if (!r)
    {
        CUE_ASSERTF(false, "Failed to initialize platform: %s (code: %s, severity: %s) at %s:%u in function %s",
            r.message.data(), Cue::to_string(r.code), Cue::to_string(r.severity),
            r.file, r.line, r.function);
    }

    // レンダリングバックエンドの作成
    auto backend = std::make_unique<RHI::DX12::D3D12Backend>();
    backend->set_win_platform(platform.get());

    // レンダリングバックエンドの初期化
    Cue::RHI::BackendSetupInfo backendInfo{};
    backendInfo.enableDebugLayer = true;
    backendInfo.width = platformInfo.width;
    backendInfo.height = platformInfo.height;
    backendInfo.bufferCount = 3;
    r = backend->initialize(backendInfo);
    if (!r)
    {
        CUE_ASSERTF(false, "Failed to initialize rendering backend: %s (code: %s, severity: %s) at %s:%u in function %s",
            r.message.data(), Cue::to_string(r.code), Cue::to_string(r.severity),
            r.file, r.line, r.function);
    }

    // エンジンの初期化
    EngineSetupInfo engineInfo{};
    engineInfo.platform = platform.get();
    engineInfo.backend = backend.get();
    engineInfo.bufferCount = backendInfo.bufferCount;
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    r = engine->initialize(engineInfo);

    // プラットフォームの開始
    r = platform->start();

    // メインループ
    bool isRunning = true;
    while (isRunning)
    {
        // プラットフォームのメッセージを処理
        PAL::PlatformMessage msg = platform->poll_message();
        if (msg == PAL::PlatformMessage::Quit)
        {
            isRunning = false;
            break;
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

    // レンダリングバックエンドのシャットダウン
    backend->shutdown();

    // プラットフォームのシャットダウン
    platform->shutdown();

    return 0;
}
