#pragma once

namespace Cue::Platform::Win
{
    // Win32 の実体型(HWND)を公開ヘッダへ漏らさないための透過ハンドル
    using NativeWindowHandle = void*;
}
