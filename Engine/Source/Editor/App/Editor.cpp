// === Base includes ===
#include <CueAssert.h>
#include <CueResult.h>

// === Core includes ===
#include <CQRS/CQRS.h>
#include <DebugTool/PerformanceCounter.h>
#include <IO/Logger.h>
#include <Time/FrameCounter.h>

// === WinPlatform includes ===
#include <win_platform.h>

// === D3D12Backend includes ===
#include <D3D12Backend.h>

// === Editor includes ===

// === Engine includes ===
#include <Engine.h>

// === ImGui includes ===
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

// === C++ includes ===
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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
    Result r = Result::ok();// 結果コード
    std::unique_ptr<PAL::Win::WinPlatform> platform = nullptr; // Windows プラットフォーム
    std::unique_ptr<RHI::DX12::D3D12Backend> renderBackend = nullptr; // D3D12 バックエンド

    return 0;
}
