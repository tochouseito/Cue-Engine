// === Base includes ===
#include <Result.h>
#include <CueAssert.h>

// === Windows includes ===
#include <win_platform.h>

// === d3d12_backend includes ===
#include <d3d12_backend.h>

// === Engine includes ===
#include <Engine.h>

// === Editor includes ===
#include "ImGuiManager.h"

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

    // ImGui マネージャの初期化
    Editor::ImGuiSetupInfo imGuiInfo(backend->buffer_count());
    imGuiInfo.hwnd = platform->get_window_handle();
    imGuiInfo.device = backend->get_device();
    imGuiInfo.commandQueue = backend->get_graphics_command_queue();
    RHI::DX12::ImGuiFontSRVInfo fontSrvInfo = backend->get_font_srv_for_imgui();
    imGuiInfo.srvDescHeap = fontSrvInfo.srvDescHeap;
    imGuiInfo.fontSrvCpuDescHandle = fontSrvInfo.cpuDescHandle;
    imGuiInfo.fontSrvGpuDescHandle = fontSrvInfo.gpuDescHandle;
    std::unique_ptr<Editor::ImGuiManager> imGuiManager = std::make_unique<Editor::ImGuiManager>(imGuiInfo);

    // ImGuiMessageHandler を登録
    const uint64_t imguiMessageHandlerId = platform->register_message_handler(
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

    // エンジンの初期化
    EngineSetupInfo engineInfo{};
    engineInfo.platform = platform.get();
    engineInfo.backend = backend.get();
    engineInfo.editorPass = std::make_unique<Editor::ImGuiPass>(*imGuiManager);
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    r = engine->initialize(engineInfo);

    RHI::ViewHandle finalColorSrvHandle{};
    r = backend->get_view_manager()->get_view("FinalColorSRV", finalColorSrvHandle);

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

        // ImGui マネージャのフレーム開始処理
        if (imGuiManager->begin_frame())
        {
            ImGui::Begin("Hello, ImGui!");

            D3D12_GPU_DESCRIPTOR_HANDLE finalColorSrvGpuDescHandle = backend->get_gpu_descriptor_handle(
                finalColorSrvHandle,
                backend->current_back_buffer_index(),
                backend->buffer_count());
            if (finalColorSrvGpuDescHandle.ptr != 0)
            {
                const float finalColorWidth = 640.0f;
                const float aspectRatio = static_cast<float>(backend->height()) / static_cast<float>(backend->width());
                ImGui::Text("FinalColor");
                ImGui::Image(
                    static_cast<ImTextureID>(finalColorSrvGpuDescHandle.ptr),
                    ImVec2(finalColorWidth, finalColorWidth * aspectRatio));
            }

            ImGui::End();
            imGuiManager->end_frame();
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
    engine.reset();

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
