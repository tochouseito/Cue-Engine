// === Base include ===
#include <Result.h>
#include <CueAssert.h>

// === Core include ===
#include <Logger.h>

// === Platform include ===
#include <win_platform.h>

// === Windows include ===
#define WIN32_LEAN_AND_MEAN             // windows ヘッダー軽量化
#define NOMINMAX                        // min と max マクロ抑止
#include <Windows.h>

// === GraphicsBackend include ===
#include <d3d12_backend.h>

// === Engine include ===
#include <Engine.h>

// === Editor include ===
#include "ImGuiManager.h"

// === C++ include ===
#include <memory>
#include <cstdint>
#include <deque>

// === ImGui include ===
#include <imgui.h>

// windows アプリのエントリーポイント
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    // 1) プラットフォームとグラフィックスバックエンドの作成
    auto win = std::make_unique<Cue::Platform::Win::WinPlatform>();
    auto d3d12Backend = std::make_unique<Cue::GraphicsCore::DX12::D3D12Backend>();

    // 2) win platform を d3d12 backend へ設定
    d3d12Backend->set_win_platform(win.get());

    // 3) win platform 初期化
    win->setup();

    // 4) d3d12 backend 初期化
    Cue::GraphicsCore::backend_setup_info backendSetupInfo{};
    backendSetupInfo.bufferCount = 3;
    backendSetupInfo.screenWidth = win->window_width();
    backendSetupInfo.screenHeight = win->window_height();
    d3d12Backend->initialize(backendSetupInfo);

    // 4) editor 初期化

    // 4-1) ImGuiManager 初期化設定を構築
    Cue::Editor::imgui_setup_info imguiSetupInfo;
    imguiSetupInfo.hwnd = static_cast<HWND>(win->get_native_window_handle());   // ImGui_ImplWin32_WndProcHandler へ渡すために HWND にキャスト
    imguiSetupInfo.device = d3d12Backend->get_device();                         // ImGui_ImplDX12_Init へ渡すために ID3D12Device* を取得
    imguiSetupInfo.commandQueue = d3d12Backend->get_graphics_command_queue();   // ImGui_ImplDX12_Init へ渡すために ID3D12CommandQueue* を取得
    imguiSetupInfo.rtvFormat = d3d12Backend->get_rtv_format();                  // ImGui_ImplDX12_Init へ渡すために RTV のフォーマットを取得
    Cue::GraphicsCore::DX12::font_srv_for_imgui fontSrvInfo = d3d12Backend->get_font_srv_for_imgui();// ImGui_ImplDX12_Init へ渡すために フォント用の SRV 情報を取得
    imguiSetupInfo.srvDescHeap = fontSrvInfo.srvDescHeap;                       // ImGui_ImplDX12_Init へ渡すために SRV デスクリプタヒープを取得
    imguiSetupInfo.fontSrvCpuDescHandle = fontSrvInfo.cpuDescHandle;            // ImGui_ImplDX12_Init へ渡すために フォント用 SRV の CPU デスクリプタハンドルを取得
    imguiSetupInfo.fontSrvGpuDescHandle = fontSrvInfo.gpuDescHandle;            // ImGui_ImplDX12_Init へ渡すために フォント用 SRV の GPU デスクリプタハンドルを取得

    Cue::Editor::ImGuiManager imguiManager;
    imguiManager.initialize(imguiSetupInfo);

    // 4-2) ImGuiMessageHandler を登録
    const uint64_t imguiMessageHandlerId = win->register_message_handler(
        [](Cue::Platform::Win::NativeWindowHandle hwnd, uint32_t msg, std::uintptr_t wParam, std::intptr_t lParam, std::intptr_t& outResult)
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

    // 5) エンジンの初期化

    // 5-1) 初期化情報構築
    Cue::EngineInitInfo initInfo;
    initInfo.platform = win.get();
    initInfo.graphicsBackend = d3d12Backend.get();

    // 5-2) editor pass として ImGuiPass を登録
    std::unique_ptr<Cue::Editor::ImGuiPass> imguiPass = std::make_unique<Cue::Editor::ImGuiPass>(imguiManager);
    initInfo.editorPass = std::move(imguiPass);

    Cue::Engine engine;
    bool isRunning = engine.initialize(initInfo);

    // 6) メインループ
    while (isRunning)
    {
        // 6-1) Windows のメッセージ処理
        isRunning = win->poll_message();

        // 6-2) imgui フレーム開始
        if (imguiManager.begin_frame())
        {
            ImGui::Begin("Hello, ImGui!"); // ウィンドウ開始
            ImGui::Text("This is a simple text in the ImGui window."); // テキスト表示
            Cue::FrameController& frameController = engine.frame_controller();
            const uint64_t totalFrame = frameController.total_frame();
            const float fps = static_cast<float>(frameController.frame_counter().fps());
            const uint32_t updateIndex = frameController.update_index();
            const uint32_t renderIndex = frameController.render_index();
            const uint32_t presentIndex = frameController.present_index();
            constexpr uint32_t kFrameBufferingCount = 3;
            const uint32_t finalColorPreviewIndex = static_cast<uint32_t>(totalFrame % kFrameBufferingCount);
            struct FrameLogLine final
            {
                uint64_t totalFrame = 0;
                float fps = 0.0f;
                uint32_t updateIndex = 0;
                uint32_t renderIndex = 0;
                uint32_t presentIndex = 0;
            };
            static std::deque<FrameLogLine> frameLogs{};
            constexpr size_t kMaxFrameLogs = 120;
            if (frameLogs.empty() || frameLogs.back().totalFrame != totalFrame)
            {
                frameLogs.push_back(FrameLogLine{
                    totalFrame,
                    fps,
                    updateIndex,
                    renderIndex,
                    presentIndex });
                if (frameLogs.size() > kMaxFrameLogs)
                {
                    frameLogs.pop_front();
                }
            }
            ImGui::BeginChild("FrameLogConsole", ImVec2(0.0f, 140.0f), true);
            for (const FrameLogLine& line : frameLogs)
            {
                ImGui::Text(
                    "Frame: %llu, FPS: %.2f, UpdateIndex: %u, RenderIndex: %u, PresentIndex: %u",
                    static_cast<unsigned long long>(line.totalFrame),
                    line.fps,
                    line.updateIndex,
                    line.renderIndex,
                    line.presentIndex);
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
            {
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();
            // fps 詳細表示切替
            static bool showFpsDetails = false;
            static float maxFps = 0;
            static float minFps = 0;
            if (ImGui::Button("Show FPS Details"))
            {
                showFpsDetails = !showFpsDetails;
                maxFps = fps;
                minFps = fps;
            }
            if (showFpsDetails)
            {
                if (fps > maxFps)
                {
                    maxFps = fps;
                }
                if (fps < minFps)
                {
                    minFps = fps;
                }
                ImGui::Text("Max FPS: %.1f", maxFps);
                ImGui::Text("Min FPS: %.1f", minFps);
            }

            // 2) 現在フレームの final color srv を取得
            Cue::CQRS::Queries::FinalColorPreviewQuery finalColorPreviewQuery(finalColorPreviewIndex);
            Cue::CQRS::Queries::TexturePreviewQueryResult finalColorPreviewResult{};
            const Cue::Result getFinalColorDescriptorResult = engine.execute_editor_query(finalColorPreviewQuery, finalColorPreviewResult);
            if (getFinalColorDescriptorResult && finalColorPreviewResult.descriptorHandle.shaderVisible)
            {
                const float viewportWidth = static_cast<float>(win->window_width());
                const float viewportHeight = static_cast<float>(win->window_height());
                const float aspectRatio = (viewportHeight > 0.0f) ? (viewportWidth / viewportHeight) : 1.0f;
                ImVec2 imageSize = ImGui::GetContentRegionAvail();
                if (imageSize.x <= 0.0f)
                {
                    imageSize.x = 320.0f;
                }
                imageSize.y = imageSize.x / aspectRatio;
                if (imageSize.y > 320.0f)
                {
                    imageSize.y = 320.0f;
                    imageSize.x = imageSize.y * aspectRatio;
                }

                ImGui::Separator();
                ImGui::Text("FinalColor Preview");
                ImGui::Image(static_cast<ImTextureID>(finalColorPreviewResult.descriptorHandle.gpuPtr), imageSize);
            }
            else
            {
                ImGui::Separator();
                ImGui::Text("FinalColor Preview");
                ImGui::Text("FinalColor descriptor is not ready.");
            }
            ImGui::End();
            imguiManager.end_frame();
        }

        // 5-2) エンジンの更新と描画
        engine.tick();
    }

    if (imguiMessageHandlerId != 0)
    {
        (void)win->unregister_message_handler(imguiMessageHandlerId);
    }

    // 6) editor シャットダウン
    imguiManager.shutdown();

    // 7) エンジンのシャットダウン
    engine.shutdown();

    // 8) グラフィックスバックエンドのシャットダウン
    d3d12Backend->shutdown();

    // 9) プラットフォームのシャットダウン
    win->shutdown();

    // 10) アプリケーションの終了
    return 0;
}
