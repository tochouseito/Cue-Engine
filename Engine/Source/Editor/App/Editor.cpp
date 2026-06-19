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

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

namespace
{
bool g_observerViewEnabled = false;
bool g_controlObserverCamera = false;

[[nodiscard]] bool is_key_down(int virtualKey) noexcept
{
    return (::GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

[[nodiscard]] bool was_key_pressed(int virtualKey) noexcept
{
    return (::GetAsyncKeyState(virtualKey) & 0x0001) != 0;
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

[[nodiscard]] double pass_gpu_ms(
    const RHI::FrameGraphExecutionStats &stats,
    std::initializer_list<std::string_view> passNames) noexcept
{
    double total = 0.0;
    for (const RHI::FrameGraphExecutionStats::PassExecutionStats &pass :
         stats.passStats)
    {
        if (!pass.hasGpuExecuteMs)
        {
            continue;
        }
        for (std::string_view passName : passNames)
        {
            if (pass.name == passName)
            {
                total += pass.gpuExecuteMs;
                break;
            }
        }
    }
    return total;
}

class ImGuiOverlayPass final : public RHI::FrameGraphPass
{
  public:
    ImGuiOverlayPass(HWND hwnd, RHI::DX12::D3D12Backend &backend,
                     Engine &engine)
        : m_backend(backend), m_engine(engine)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = "config/editor/imgui.ini";

        ImFontConfig fontConfig{};
        fontConfig.OversampleH = 3;
        fontConfig.OversampleV = 2;
        fontConfig.RasterizerMultiply = 1.45f;
        if (io.Fonts->AddFontFromFileTTF(
                "EngineResources/Fonts/NotoSansJP-VariableFont_wght.ttf",
                18.0f, &fontConfig) == nullptr)
        {
            io.Fonts->AddFontFromFileTTF(
                "EngineResources/Fonts/Inter-VariableFont_opsz,wght.ttf",
                18.0f, &fontConfig);
        }
        ImGui::StyleColorsDark();
        ImGuiStyle &style = ImGui::GetStyle();
        style.Colors[ImGuiCol_Text] = ImVec4(0.98f, 0.98f, 0.98f, 1.0f);
        style.Colors[ImGuiCol_TextDisabled] =
            ImVec4(0.72f, 0.72f, 0.72f, 1.0f);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.05f, 0.06f, 0.94f);

        ImGui_ImplWin32_Init(hwnd);

        ImGui_ImplDX12_InitInfo initInfo{};
        initInfo.Device = backend.imgui_device();
        initInfo.CommandQueue = backend.imgui_command_queue();
        initInfo.NumFramesInFlight = 3;
        initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
        initInfo.UserData = &backend;
        initInfo.SrvDescriptorHeap = backend.imgui_srv_descriptor_heap();
        initInfo.SrvDescriptorAllocFn = &allocate_srv_descriptor;
        initInfo.SrvDescriptorFreeFn = &free_srv_descriptor;
        m_initialized = ImGui_ImplDX12_Init(&initInfo);
    }

    ~ImGuiOverlayPass() override
    {
        if (m_initialized)
        {
            ImGui_ImplDX12_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        }
    }

    const char *name() const noexcept override
    {
        return "ImGuiOverlay";
    }

    RHI::CommandListType type() const noexcept override
    {
        return RHI::CommandListType::Graphics;
    }

    Result setup(RHI::FrameGraphBuilder &builder) override
    {
        Result result = builder.get_texture("BackBuffer", m_backBuffer);
        if (!result)
        {
            return result;
        }
        result = builder.render(&m_backBuffer, 1);
        if (!result)
        {
            return result;
        }
        return builder.get_view("BackBufferRTV", m_backBufferRtv);
    }

    Result describe_resources(RHI::FrameGraphBuilder &builder) override
    {
        return builder.use_texture(
            m_backBuffer, RHI::ResourceAccessType::Write,
            RHI::ResourceState::RenderTarget, RHI::ResourceState::Present);
    }

    void execute(RHI::FrameGraphContext &context) override
    {
        if (!m_initialized || context.commandContext() == nullptr)
        {
            return;
        }

        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        draw_overlay();
        ImGui::Render();

        RHI::ICommandContext *commandContext = context.commandContext();
        commandContext->set_render_targets(&m_backBufferRtv, 1, {});
        commandContext->set_viewport_scissor(context.width(), context.height());

        auto *commandList = static_cast<ID3D12GraphicsCommandList *>(
            commandContext->native_command_list());
        ID3D12DescriptorHeap *descriptorHeaps[] = {
            m_backend.imgui_srv_descriptor_heap()};
        commandList->SetDescriptorHeaps(1, descriptorHeaps);
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
    }

  private:
    static void allocate_srv_descriptor(
        ImGui_ImplDX12_InitInfo *info,
        D3D12_CPU_DESCRIPTOR_HANDLE *outCpuHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE *outGpuHandle)
    {
        auto *backend =
            static_cast<RHI::DX12::D3D12Backend *>(info->UserData);
        backend->allocate_imgui_srv_descriptor(*outCpuHandle, *outGpuHandle);
    }

    static void free_srv_descriptor(
        ImGui_ImplDX12_InitInfo *info,
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle)
    {
        auto *backend =
            static_cast<RHI::DX12::D3D12Backend *>(info->UserData);
        backend->free_imgui_srv_descriptor(cpuHandle, gpuHandle);
    }

    void draw_overlay()
    {
        const EngineDebugStats debugStats = m_engine.debug_stats();
        const RHI::FrameGraphExecutionStats frameStats =
            m_engine.render_execution_stats();

        bool directionalLightEnabled = debugStats.directionalLightEnabled;

        ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(520.0f, 760.0f),
                                 ImGuiCond_FirstUseEver);
        ImGui::Begin("CueEngineRef GPU Driven Demo");

        ImGui::Text("Frame");
        const float fps = ImGui::GetIO().Framerate;
        ImGui::Text("FPS / Frame Time: %.1f / %.3f ms", fps,
                    fps > 0.0f ? 1000.0f / fps : 0.0f);
        ImGui::Text("GPU Frame Time: %s%.3f ms",
                    frameStats.hasGpuFrameMs ? "" : "~",
                    frameStats.hasGpuFrameMs ? frameStats.gpuFrameMs
                                             : frameStats.totalExecuteMs);
        ImGui::TextDisabled(
            "Object/draw counters are CPU-side estimates until GPU readback is added.");

        if (ImGui::CollapsingHeader("Pass GPU Time",
                                    ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("BuildClusterGrid: %.3f ms",
                        pass_gpu_ms(frameStats, {"BuildClusterGrid"}));
            ImGui::Text("PreparePointLights: %.3f ms",
                        pass_gpu_ms(frameStats, {"PreparePointLights"}));
            ImGui::Text("ClusterLightCulling: %.3f ms",
                        pass_gpu_ms(frameStats, {"ClusterLightCulling"}));
            ImGui::Text("ObjectCullAndLod: %.3f ms",
                        pass_gpu_ms(frameStats, {"ObjectCullAndLod"}));
            ImGui::Text("OccluderDepthOnlyIndirect: %.3f ms",
                        pass_gpu_ms(frameStats,
                                    {"OccluderDepthOnlyIndirect"}));
            ImGui::Text("BuildHiZ: %.3f ms",
                        pass_gpu_ms(frameStats, {"BuildHiZDepth"}));
            ImGui::Text("CellCulling: %.3f ms",
                        pass_gpu_ms(frameStats, {"CellCulling"}));
            ImGui::Text("ObjectCulling: %.3f ms",
                        pass_gpu_ms(frameStats, {"ObjectCulling"}));
            ImGui::Text("Batching: %.3f ms",
                        pass_gpu_ms(frameStats,
                                    {"BatchCount", "PrefixSum", "BatchFill",
                                     "IndirectCommandEmit"}));
            ImGui::Text("StaticMeshForward: %.3f ms",
                        pass_gpu_ms(frameStats, {"StaticMeshForward"}));
        }

        if (ImGui::CollapsingHeader("Clustered Lighting",
                                    ImGuiTreeNodeFlags_DefaultOpen))
        {
            // ClusterLightCulling が GPU 上で集計した値。
            // pass time と並べて見ることで、cluster grid や compact list の
            // チューニングが効いているか判断する。
            const GpuData::ClusterLightingStatsGpu &clusterStats =
                debugStats.clusterLightingStats;
            const float avgLightsPerCluster =
                clusterStats.clusterCount > 0u
                    ? static_cast<float>(clusterStats.totalClusterItems) /
                          static_cast<float>(clusterStats.clusterCount)
                    : 0.0f;

            ImGui::Text("BuildClusterGrid: %.3f ms",
                        pass_gpu_ms(frameStats, {"BuildClusterGrid"}));
            ImGui::Text("PreparePointLights: %.3f ms",
                        pass_gpu_ms(frameStats, {"PreparePointLights"}));
            ImGui::Text("ClusterLightCulling: %.3f ms",
                        pass_gpu_ms(frameStats, {"ClusterLightCulling"}));
            ImGui::Text("StaticMeshForward: %.3f ms",
                        pass_gpu_ms(frameStats, {"StaticMeshForward"}));
            ImGui::Separator();
            ImGui::Text("clusterCount: %u", clusterStats.clusterCount);
            ImGui::Text("activeClusterCount: %u",
                        clusterStats.activeClusterCount);
            ImGui::Text("pointLightCount: %u",
                        clusterStats.pointLightCount);
            ImGui::Text("totalClusterItems: %u",
                        clusterStats.totalClusterItems);
            ImGui::Text("avg lights / cluster: %.2f",
                        avgLightsPerCluster);
            ImGui::Text("max lights / cluster: %u",
                        clusterStats.maxLightsInCluster);
            ImGui::Text("overflow clusters: %u",
                        clusterStats.overflowClusterCount);
            ImGui::Text("empty clusters: %u",
                        clusterStats.emptyClusterCount);
            ImGui::Text("reused light lists: %u",
                        clusterStats.reusedListCount);
            ImGui::TextDisabled("Cluster stats are GPU readback values with a small frame delay.");
        }

        if (ImGui::CollapsingHeader("Objects",
                                    ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("total objects: %u", debugStats.totalObjects);
            ImGui::Text("visible objects: %u", debugStats.visibleObjects);
            ImGui::Text("occluded objects: %u", debugStats.occludedObjects);
            ImGui::Text("culled by frustum: %u",
                        debugStats.frustumCulledObjects);
        }

        if (ImGui::CollapsingHeader("Draw",
                                    ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("indirect draw count: %u",
                        debugStats.indirectDrawCount);
            ImGui::Text("instance count: %u", debugStats.instanceCount);
            ImGui::Text("triangle estimate: %llu",
                        static_cast<unsigned long long>(
                            debugStats.submittedTriangleEstimate));
        }

        if (ImGui::CollapsingHeader("LOD Distribution",
                                    ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("LOD0: %u", debugStats.lodObjectCounts[0]);
            ImGui::Text("LOD1: %u", debugStats.lodObjectCounts[1]);
            ImGui::Text("LOD2: %u", debugStats.lodObjectCounts[2]);
            ImGui::Text("LOD3: %u", debugStats.lodObjectCounts[3]);
            ImGui::Text("LOD4: %u", debugStats.lodObjectCounts[4]);
            ImGui::Text("impostor: %u", debugStats.impostorCount);
        }

        if (ImGui::CollapsingHeader("Occluder",
                                    ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("occluder object count: %u",
                        debugStats.occluderObjectCount);
            ImGui::Text("occluder triangle count: %llu",
                        static_cast<unsigned long long>(
                            debugStats.occluderTriangleEstimate));
            ImGui::Text("occluder proxy: %s",
                        debugStats.occluderProxyEnabled ? "ON" : "OFF");
            ImGui::Text("Hi-Z: %s", debugStats.hiZEnabled ? "ON" : "OFF");
        }

        if (ImGui::CollapsingHeader("Toggles",
                                    ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Frustum Culling", &m_frustumCullingEnabled);
            ImGui::Checkbox("Hi-Z Occlusion", &m_hiZEnabled);
            ImGui::Checkbox("Occluder Proxy", &m_occluderProxyEnabled);
            ImGui::Checkbox("LOD Selection", &m_lodEnabled);
            ImGui::Checkbox("Impostor", &m_impostorEnabled);
            if (ImGui::Checkbox("Directional Light",
                                &directionalLightEnabled))
            {
                m_engine.set_directional_light_enabled(
                    directionalLightEnabled);
            }
            ImGui::Checkbox("Point Lights", &m_pointLightsEnabled);
            ImGui::TextDisabled(
                "Only Directional Light is wired to the renderer.");
        }

        if (ImGui::CollapsingHeader("Camera / Debug",
                                    ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("camera position: %.2f, %.2f, %.2f",
                        debugStats.cameraPosition.x,
                        debugStats.cameraPosition.y,
                        debugStats.cameraPosition.z);
            ImGui::Text("observer view: %s",
                        g_observerViewEnabled ? "ON" : "OFF");
            ImGui::Text("camera control: %s",
                        g_controlObserverCamera ? "observer" : "main/culling");
            ImGui::Text("visible cells / total cells: %u / %u",
                        debugStats.visibleCells, debugStats.totalCells);
            ImGui::Text("selected depth bin: %u",
                        debugStats.selectedDepthBin);
        }

        if (ImGui::CollapsingHeader("Render Cost",
                                    ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("submitted triangles: %llu",
                        static_cast<unsigned long long>(
                            debugStats.submittedTriangleEstimate));
            ImGui::Text("saved triangles estimate: %llu",
                        static_cast<unsigned long long>(
                            debugStats.savedTriangleEstimate));
            ImGui::Text("saved objects estimate: %u",
                        debugStats.savedObjectEstimate);
        }

        if (ImGui::CollapsingHeader("Controls",
                                    ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::BulletText("W/A/S/D: move camera");
            ImGui::BulletText("Space / Ctrl: move up / down");
            ImGui::BulletText("Shift: fast movement");
            ImGui::BulletText("Right mouse drag: look around");
            ImGui::BulletText("C: toggle observer view");
            ImGui::BulletText("Tab: switch main / observer camera");
            ImGui::BulletText("Mouse over this window: operate ImGui");
        }

        ImGui::End();
    }

    RHI::DX12::D3D12Backend &m_backend;
    Engine &m_engine;
    RHI::TextureHandle m_backBuffer{};
    RHI::ViewHandle m_backBufferRtv{};
    bool m_initialized = false;
    bool m_frustumCullingEnabled = true;
    bool m_hiZEnabled = true;
    bool m_occluderProxyEnabled = true;
    bool m_lodEnabled = true;
    bool m_impostorEnabled = true;
    bool m_pointLightsEnabled = true;
};

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
    const Math::uint3 modelGridCount(100u, 2u, 100u);
    const float modelTargetRadius = 0.6f;
    const uint32_t maxPointLightCount = 5000;
    const bool enableDirectionalLight = false;

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
    std::unique_ptr<RHI::FrameGraphPass> imguiOverlayPass =
        std::make_unique<ImGuiOverlayPass>(
            platform->get_window_handle(), *renderBackend, *engine);
    platform->set_message_handler(
        [](HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
           LRESULT &outResult) -> bool
        {
            outResult =
                ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam);
            return outResult != 0;
        });

    EngineSetupInfo engineSetupInfo{};
    engineSetupInfo.maxFps = maxFps; // 最大フレームレートを Engine にセット
    engineSetupInfo.maxPointLightCount = maxPointLightCount;
    engineSetupInfo.enableDirectionalLight = enableDirectionalLight;
    engineSetupInfo.editorPass = std::move(imguiOverlayPass);
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
    Editor::DebugCamera observerCamera{
        Math::float3(-12.0f, 6.0f, -12.0f),
        45.0f * Editor::DebugCameraConstants::k_pi / 180.0f,
        -0.35f};
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
                "StanfordDragon", {0.50f, 0.15f, 0.01f}, true},
            Editor::ModelImporter::LodGroupSettings{
                "AsianDragon", {0.25f, 0.05f, 0.005f}, true},
            Editor::ModelImporter::LodGroupSettings{
                "Bunny", {0.50f, 0.15f, 0.01f}, true},
            Editor::ModelImporter::LodGroupSettings{
                "Buddha", {0.20f, 0.03f, 0.003f}, true},
    };

    constexpr std::array<TestModelDesc, 4> k_testModels = {
        TestModelDesc{"stanforddragon.obj", "stanforddragon", 0u},
        TestModelDesc{"asiandragon.obj", "asiandragon", 1u},
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

        if (was_key_pressed('C'))
        {
            g_observerViewEnabled = !g_observerViewEnabled;
        }
        if (was_key_pressed(VK_TAB))
        {
            g_controlObserverCamera = !g_controlObserverCamera;
            g_observerViewEnabled = g_controlObserverCamera;
        }

        Editor::DebugCamera::Input cameraInput = make_debug_camera_input(
            platform->get_window_handle(), deltaSeconds);
        if (g_controlObserverCamera)
        {
            observerCamera.update(cameraInput);
        }
        else
        {
            debugCamera.update(cameraInput);
        }

        const GpuData::ViewProjectionGpu mainViewProjection =
            debugCamera.make_view_projection(width, height);
        r = engine->set_view_projection(mainViewProjection);
        if (!r)
        {
            CUE_ASSERT_FORMAT(false, "Failed to update debug camera: %s",
                              r.message.data());
            return -1;
        }
        if (g_observerViewEnabled)
        {
            r = engine->set_render_view_projection(
                observerCamera.make_view_projection(width, height));
        }
        else
        {
            r = engine->set_render_view_projection(mainViewProjection);
        }
        if (!r)
        {
            CUE_ASSERT_FORMAT(false, "Failed to update render camera: %s",
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
