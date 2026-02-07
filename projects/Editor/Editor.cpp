#include <Windows.h>
#include <iostream>

// Platform
#include <win_platform.h>

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    bool isRunning = true;
    Cue::Platform::Win::WinPlatform platform;
    platform.setup();
    platform.start();
    while (isRunning)
    {
        isRunning = platform.poll_message();
        // ここに更新処理や描画処理を追加可能
    }

    return 0;
}
