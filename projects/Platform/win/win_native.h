#pragma once

namespace Cue::Platform::Win
{
    // Win32 実体型(HWND)を隠蔽する透過ハンドル
    using NativeWindowHandle = void*;
}
