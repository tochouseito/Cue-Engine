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
#include "DebugCamera.h"
#include <Asset/ModelImporter.h>

// === Engine includes ===
#include <Engine.h>

// === C++ includes ===
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

using namespace Cue;

namespace
{
[[nodiscard]] bool is_key_down(int virtualKey) noexcept
{
    return (::GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

[[nodiscard]] Editor::DebugCamera::Input make_debug_camera_input(
    HWND windowHandle, float deltaSeconds) noexcept
{
    Editor::DebugCamera::Input input{};
    input.deltaSeconds = deltaSeconds;

    if (windowHandle == nullptr || ::GetForegroundWindow() != windowHandle)
    {
        return input;
    }

    input.moveForward = is_key_down('W');
    input.moveBackward = is_key_down('S');
    input.moveLeft = is_key_down('A');
    input.moveRight = is_key_down('D');
    input.moveUp = is_key_down(VK_SPACE);
    input.moveDown = is_key_down(VK_CONTROL);
    input.fast = is_key_down(VK_SHIFT);
    input.lookActive = is_key_down(VK_RBUTTON);

    static bool hadPreviousMousePosition = false;
    static POINT previousMousePosition{};

    POINT currentMousePosition{};
    if (!::GetCursorPos(&currentMousePosition))
    {
        hadPreviousMousePosition = false;
        return input;
    }

    if (input.lookActive)
    {
        if (!hadPreviousMousePosition)
        {
            previousMousePosition = currentMousePosition;
            hadPreviousMousePosition = true;
            ::SetCapture(windowHandle);
        }
        else
        {
            input.mouseDeltaX = static_cast<float>(currentMousePosition.x -
                                                   previousMousePosition.x);
            input.mouseDeltaY = static_cast<float>(currentMousePosition.y -
                                                   previousMousePosition.y);
            previousMousePosition = currentMousePosition;
        }
    }
    else
    {
        if (hadPreviousMousePosition)
        {
            ::ReleaseCapture();
        }
        hadPreviousMousePosition = false;
    }

    return input;
}

} // namespace

// windows アプリのエントリーポイント
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    // パラメーター
    uint32_t width = 1920;
    uint32_t height = 1080;
    const char *className = "CueEditorWindowClass";
    const char *title = "Cue Editor";
    uint32_t maxFps = 0;
    uint32_t bufferCount = 3;
    const Math::uint3 modelGridCount(100u, 10u, 20u);
    const float modelTargetRadius = 0.6f;
    const uint32_t maxPointLightCount = 1000;
    const bool enableDirectionalLight = true;

    // 処理結果
    Result r = Result::ok();

    // プラットフォーム実装を初期化
    std::unique_ptr<PAL::Win::WinPlatform> platform =
        std::make_unique<PAL::Win::WinPlatform>();
    std::unique_ptr<Core::CQRS::Bridge> commandBridge =
        std::make_unique<Core::CQRS::Bridge>();
    platform->set_command_bridge(
        commandBridge.get()); // コマンドブリッジをプラットフォームにセット
    PAL::PlatformSetupInfo setupInfo{};
    setupInfo.width = width;
    setupInfo.height = height;
    setupInfo.className = className;
    setupInfo.title = title;
    r = platform->initialize(setupInfo);

    // 失敗したらエラーを表示して終了
    if (!r)
    {
        CUE_ASSERT_FORMAT(false, "Failed to initialize platform: %s",
                          r.message.data());
        Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                      "Failed to initialize platform: %s", r.message.data());
        return -1;
    }

    // Logger にプラットフォームのファイルシステムをセット
    Core::IO::set_log_file(platform->file_system(),
                           Core::IO::Path("logs/editor.log"), true);

    // PerformanceCounter を初期化
    Core::PerformanceCounter profiler(platform->clock());

    // レンダーバックエンドを初期化
    std::unique_ptr<RHI::DX12::D3D12Backend> renderBackend =
        std::make_unique<RHI::DX12::D3D12Backend>();
    RHI::RenderBackendSetupInfo renderBackendSetupInfo{};
#ifdef CUE_DEBUG
    bool enableDebugLayer = true;
#else
    bool enableDebugLayer = false;
#endif
    renderBackendSetupInfo.enableDebugLayer = enableDebugLayer;
    renderBackendSetupInfo.width = width;
    renderBackendSetupInfo.height = height;
    renderBackendSetupInfo.bufferCount = bufferCount;
    renderBackend->set_win_platform(
        platform.get()); // Windows プラットフォームをバックエンドにセット
    r = renderBackend->initialize(renderBackendSetupInfo);

    // 失敗したらエラーを表示して終了
    if (!r)
    {
        CUE_ASSERT_FORMAT(false, "Failed to initialize render backend: %s",
                          r.message.data());
        Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                      "Failed to initialize render backend: %s",
                      r.message.data());
        return -1;
    }

    // Engine を初期化
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    EngineSetupInfo engineSetupInfo{};
    engineSetupInfo.maxFps = maxFps; // 最大フレームレートを Engine にセット
    engineSetupInfo.maxPointLightCount = maxPointLightCount;
    engineSetupInfo.enableDirectionalLight = enableDirectionalLight;
    engineSetupInfo.platform =
        platform.get(); // プラットフォームを Engine にセット
    engineSetupInfo.platformCommandBridge =
        commandBridge.get(); // コマンドブリッジを Engine にセット
    engineSetupInfo.renderBackend =
        renderBackend.get(); // レンダーバックエンドを Engine にセット
    r = engine->initialize(engineSetupInfo);
    if (!r)
    {
        CUE_ASSERT_FORMAT(false, "Failed to initialize engine: %s",
                          r.message.data());
        Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                      "Failed to initialize engine: %s", r.message.data());
        return -1;
    }

    Editor::DebugCamera debugCamera{};
    r = engine->set_view_projection(
        debugCamera.make_view_projection(width, height));
    if (!r)
    {
        CUE_ASSERT_FORMAT(false, "Failed to set debug camera: %s",
                          r.message.data());
        Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                      "Failed to set debug camera: %s", r.message.data());
        return -1;
    }

    struct TestModelDesc final
    {
        const char *fileName = nullptr;
        const char *modelName = nullptr;
        uint32_t lodGroupIndex = 0;
    };

    constexpr std::array<Editor::ModelImporter::LodGroupSettings, 4>
        k_lodGroups = {
            Editor::ModelImporter::LodGroupSettings{
                "Dragon", {0.50f, 0.15f, 0.01f}, true},
            Editor::ModelImporter::LodGroupSettings{
                "DragonHighPoly", {0.25f, 0.05f, 0.005f}, true},
            Editor::ModelImporter::LodGroupSettings{
                "Bunny", {0.50f, 0.15f, 0.01f}, true},
            Editor::ModelImporter::LodGroupSettings{
                "Buddha", {0.20f, 0.03f, 0.003f}, true},
        };

    constexpr std::array<TestModelDesc, 4> k_testModels = {
        TestModelDesc{"dragon.obj", "dragon", 0u},
        TestModelDesc{"dragon-highpoly.obj", "dragon-highpoly", 1u},
        TestModelDesc{"bunny.obj", "bunny", 2u},
        TestModelDesc{"buddha.obj", "buddha", 3u},
    };

    std::vector<Core::Native::ModelData> modelDataList{};
    modelDataList.reserve(k_testModels.size());
    for (const TestModelDesc &modelDesc : k_testModels)
    {
        Core::Native::ModelData modelData{};
        const Core::IO::Path modelPath(std::string(CUE_PROJECT_ROOT_PATH) +
                                       "/TestProject/Assets/Models/" +
                                       modelDesc.fileName);
        if (modelDesc.lodGroupIndex >= k_lodGroups.size())
        {
            CUE_ASSERT_FORMAT(false, "Invalid LOD group index for model '%s'.",
                              modelDesc.modelName);
            return -1;
        }

        r = Editor::ModelImporter::import_model(
            modelPath, modelDesc.modelName,
            k_lodGroups[modelDesc.lodGroupIndex], modelData);
        if (!r)
        {
            CUE_ASSERT_FORMAT(false, "Failed to import model '%s': %s",
                              modelDesc.modelName, r.message.data());
            Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                          "Failed to import model '{}': {}",
                          modelDesc.modelName, r.message);
            return -1;
        }
        modelDataList.push_back(std::move(modelData));
    }

    r = engine->register_models(modelDataList, modelGridCount,
                                modelTargetRadius);
    if (!r)
    {
        CUE_ASSERT_FORMAT(false, "Failed to register test models: %s",
                          r.message.data());
        Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                      "Failed to register test models: %s", r.message.data());
        return -1;
    }

    // ウィンドウ表示を開始
    r = platform->start();
    if (!r)
    {
        CUE_ASSERT_FORMAT(false, "Failed to start platform: %s",
                          r.message.data());
        Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                      "Failed to start platform: %s", r.message.data());
        return -1;
    }

    // プロセスメモリ、システムメモリ使用量をログに出力
    PAL::ProcessMemoryUsage processMemoryUsage{};
    PAL::SystemMemoryUsage systemMemoryUsage{};
    if (r = platform->get_process_memory_usage(processMemoryUsage); r)
    {
        Core::IO::log(
            Core::IO::LogSink::console | Core::IO::LogSink::file,
            "Process Memory Usage - Working Set: {} MB, Private Bytes: {} MB",
            processMemoryUsage.workingSetBytes / (1024 * 1024),
            processMemoryUsage.privateBytes / (1024 * 1024));
    }
    else
    {
        Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                      "Failed to get process memory usage: {}", r.message);
    }
    if (r = platform->get_system_memory_usage(systemMemoryUsage); r)
    {
        Core::IO::log(
            Core::IO::LogSink::console | Core::IO::LogSink::file,
            "System Memory Usage - Total Phys: {} MB, Avail Phys: {} MB, "
            "Memory Load: {}%",
            systemMemoryUsage.totalPhysBytes / (1024 * 1024),
            systemMemoryUsage.availPhysBytes / (1024 * 1024),
            systemMemoryUsage.memoryLoadPercent);
    }
    else
    {
        Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                      "Failed to get system memory usage: {}", r.message);
    }
    // GPU
    RHI::GpuMemoryUsage gpuMemoryUsage{};
    if (r = renderBackend->get_gpu_memory_usage(gpuMemoryUsage); r)
    {
        Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                      "GPU Memory Usage - Budget: {} MB, Current Usage: {} MB, "
                      "Available for "
                      "Reservation: {} MB, Current Reservation: {} MB",
                      gpuMemoryUsage.budgetBytes / (1024 * 1024),
                      gpuMemoryUsage.currentUsageBytes / (1024 * 1024),
                      gpuMemoryUsage.availableForReservationBytes /
                          (1024 * 1024),
                      gpuMemoryUsage.currentReservationBytes / (1024 * 1024));
    }
    else
    {
        Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                      "Failed to get GPU memory usage: {}", r.message);
    }

    // メインループ
    bool isRunning = true;
    auto previousInputTime = std::chrono::steady_clock::now();
    while (isRunning)
    {
        const auto currentInputTime = std::chrono::steady_clock::now();
        const float deltaSeconds = std::clamp(
            std::chrono::duration<float>(currentInputTime - previousInputTime)
                .count(),
            0.0f, 0.1f);
        previousInputTime = currentInputTime;

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
            CUE_ASSERT_FORMAT(false, "Failed to begin frame: %s",
                              r.message.data());
            return -1;
        }

        r = engine->begin_frame();

        // 失敗したらエラーを表示して終了
        if (!r)
        {
            CUE_ASSERT_FORMAT(false, "Failed to begin engine frame: %s",
                              r.message.data());
            return -1;
        }

        debugCamera.update(make_debug_camera_input(
            platform->get_window_handle(), deltaSeconds));
        r = engine->set_view_projection(
            debugCamera.make_view_projection(width, height));
        if (!r)
        {
            CUE_ASSERT_FORMAT(false, "Failed to update debug camera: %s",
                              r.message.data());
            return -1;
        }

        // --- ここで Engine 側の更新と描画処理を呼び出す ---
        r = engine->tick();

        // 失敗したらエラーを表示して終了
        if (!r)
        {
            CUE_ASSERT_FORMAT(false, "Failed to tick engine: %s",
                              r.message.data());
            return -1;
        }

        const Core::Time::FrameCounter &frameCounter =
            engine->frame_controller().frame_counter();
        if (frameCounter.total_frames() > 0)
        {
            // Core::IO::log(Core::IO::LogSink::console, "FPS : {:.2f}",
            // frameCounter.fps());
        }
        /*profiler.begin("Test", "Update");
        profiler.end("Test", "Update");
        if (const auto snapshot = profiler.get_snapshot("Test", "Update"))
        {
            Core::IO::log(Core::IO::LogSink::console, "Update Time : {:.2f} ms",
        snapshot->timer.elapsed_seconds() * 1000.0);
        }*/

        r = engine->end_frame();

        // 失敗したらエラーを表示して終了
        if (!r)
        {
            CUE_ASSERT_FORMAT(false, "Failed to end engine frame: %s",
                              r.message.data());
            return -1;
        }

        // フレーム終了
        r = platform->end_frame();

        // 失敗したらエラーを表示して終了
        if (!r)
        {
            CUE_ASSERT_FORMAT(false, "Failed to end frame: %s",
                              r.message.data());
            return -1;
        }
    }

    Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                  "Editor shutdown");
    Core::IO::clear_log_file();

    // 終了処理
    engine->shutdown();
    engine.reset();
    renderBackend->shutdown();
    renderBackend.reset();
    platform->shutdown();
    platform.reset();

    return 0;
}
