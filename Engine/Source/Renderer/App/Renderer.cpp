// === Base includes ===
#include <CueAssert.h>
#include <CueResult.h>

// === Core includes ===
#include <CQRS/CQRS.h>
#include <IO/Logger.h>

// === WinPlatform includes ===
#include <win_platform.h>

// === Windows includes ===
#include <Xinput.h>

// === D3D12Backend includes ===
#include <D3D12Backend.h>

// === Renderer includes ===
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
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace Cue;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd,
                                                             UINT message,
                                                             WPARAM wParam,
                                                             LPARAM lParam);

namespace
{
    [[nodiscard]] Core::IO::Path
    content_path(Core::IO::IFileSystem& fileSystem,
                 std::string_view relativePath) noexcept
    {
#if defined(CUE_DEBUG) && defined(CUE_PROJECT_ROOT_PATH)
        fileSystem;
        return Core::IO::Path::join(Core::IO::Path(CUE_PROJECT_ROOT_PATH),
                                    Core::IO::Path(std::string(relativePath)));
#else
        Core::IO::Path executableDirectory{};
        if (fileSystem.executable_directory(executableDirectory))
        {
            return Core::IO::Path::join(executableDirectory,
                                        Core::IO::Path(std::string(relativePath)));
        }

        return Core::IO::Path(std::string(relativePath));
#endif
    }

    [[nodiscard]] Core::IO::Path
    content_path(Core::IO::IFileSystem& fileSystem,
                 std::string_view debugRelativePath,
                 std::string_view runtimeRelativePath) noexcept
    {
#if defined(CUE_DEBUG) && defined(CUE_PROJECT_ROOT_PATH)
        runtimeRelativePath;
        return content_path(fileSystem, debugRelativePath);
#else
        debugRelativePath;
        return content_path(fileSystem, runtimeRelativePath);
#endif
    }

    [[nodiscard]] ImFont*
    add_font_from_file_system(ImFontAtlas& fonts, Core::IO::IFileSystem& fileSystem,
                              const Core::IO::Path& path, float sizePixels,
                              ImFontConfig fontConfig,
                              std::vector<std::byte>& fontData) noexcept
    {
        fontData.clear();
        if (!fileSystem.read_all(path, &fontData) || fontData.empty() ||
            fontData.size() >
                static_cast<size_t>((std::numeric_limits<int>::max)()))
        {
            return nullptr;
        }

        fontConfig.FontDataOwnedByAtlas = false;
        return fonts.AddFontFromMemoryTTF(fontData.data(),
                                          static_cast<int>(fontData.size()),
                                          sizePixels, &fontConfig);
    }

    [[nodiscard]] bool is_key_down(int virtualKey) noexcept
    {
        return (::GetAsyncKeyState(virtualKey) & 0x8000) != 0;
    }

    [[nodiscard]] float normalize_gamepad_axis(SHORT value, SHORT deadZone) noexcept
    {
        if (value > deadZone)
        {
            return static_cast<float>(value - deadZone) /
                static_cast<float>(32767 - deadZone);
        }

        if (value < -deadZone)
        {
            return static_cast<float>(value + deadZone) /
                static_cast<float>(32768 - deadZone);
        }

        return 0.0f;
    }

    [[nodiscard]] float normalize_gamepad_trigger(BYTE value) noexcept
    {
        if (value <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
        {
            return 0.0f;
        }

        return static_cast<float>(value - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) /
            static_cast<float>(255 - XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
    }

    void apply_gamepad_input(Renderer::DebugCamera::Input& input) noexcept
    {
        XINPUT_STATE state{};
        for (DWORD userIndex = 0; userIndex < XUSER_MAX_COUNT; ++userIndex)
        {
            if (::XInputGetState(userIndex, &state) != ERROR_SUCCESS)
            {
                continue;
            }

            const XINPUT_GAMEPAD& gamepad = state.Gamepad;
            const float leftX = normalize_gamepad_axis(
                gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
            const float leftY = normalize_gamepad_axis(
                gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
            input.gamepadMoveX = leftX;
            input.gamepadMoveY = leftY;
            input.gamepadMoveVertical = normalize_gamepad_trigger(gamepad.bRightTrigger) -
                normalize_gamepad_trigger(gamepad.bLeftTrigger);
            input.fast = input.fast ||
                (gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
            input.gamepadLookX = normalize_gamepad_axis(
                gamepad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
            input.gamepadLookY = normalize_gamepad_axis(
                gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
            return;
        }
    }

    [[nodiscard]] Renderer::DebugCamera::Input
    make_debug_camera_input(HWND windowHandle, float deltaSeconds) noexcept
    {
        Renderer::DebugCamera::Input input{};
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

        apply_gamepad_input(input);

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
                input.mouseDeltaX =
                    static_cast<float>(currentMousePosition.x - previousMousePosition.x);
                input.mouseDeltaY =
                    static_cast<float>(currentMousePosition.y - previousMousePosition.y);
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

    class ImGuiOverlayPass final : public RHI::FrameGraphPass
    {
      public:
        ImGuiOverlayPass(HWND hwnd, RHI::DX12::D3D12Backend& backend, Engine& engine,
                         Core::IO::IFileSystem& fileSystem)
            : m_backend(backend), m_engine(engine), m_fileSystem(fileSystem)
        {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.IniFilename = "config/renderer/imgui.ini";

            ImFontConfig fontConfig{};
            fontConfig.OversampleH = 3;
            fontConfig.OversampleV = 2;
            fontConfig.RasterizerMultiply = 1.45f;
            if (add_font_from_file_system(
                    *io.Fonts, m_fileSystem,
                    content_path(m_fileSystem,
                                 "Engine/Fonts/"
                                 "NotoSansJP-VariableFont_wght.ttf",
                                 "EngineResources/Fonts/"
                                 "NotoSansJP-VariableFont_wght.ttf"),
                    18.0f, fontConfig, m_primaryFontData) == nullptr)
            {
                (void)add_font_from_file_system(
                    *io.Fonts, m_fileSystem,
                    content_path(m_fileSystem,
                                 "Engine/Fonts/"
                                 "Inter-VariableFont_opsz,wght.ttf",
                                 "EngineResources/Fonts/"
                                 "Inter-VariableFont_opsz,wght.ttf"),
                    18.0f, fontConfig, m_fallbackFontData);
            }
            ImGui::StyleColorsDark();
            ImGuiStyle& style = ImGui::GetStyle();
            style.Colors[ImGuiCol_Text] = ImVec4(0.98f, 0.98f, 0.98f, 1.0f);
            style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.72f, 0.72f, 0.72f, 1.0f);
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

        const char* name() const noexcept override
        {
            return "ImGuiOverlay";
        }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Graphics;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
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

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            return builder.use_texture(m_backBuffer, RHI::ResourceAccessType::Write,
                                       RHI::ResourceState::RenderTarget,
                                       RHI::ResourceState::Present);
        }

        void execute(RHI::FrameGraphContext& context) override
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

            RHI::ICommandContext* commandContext = context.commandContext();
            commandContext->set_render_targets(&m_backBufferRtv, 1, {});
            commandContext->set_viewport_scissor(context.width(), context.height());

            auto* commandList = static_cast<ID3D12GraphicsCommandList*>(
                commandContext->native_command_list());
            ID3D12DescriptorHeap* descriptorHeaps[] = {
                m_backend.imgui_srv_descriptor_heap()
            };
            commandList->SetDescriptorHeaps(1, descriptorHeaps);
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
        }

      private:
        static void
        allocate_srv_descriptor(ImGui_ImplDX12_InitInfo* info,
                                D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle,
                                D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle)
        {
            auto* backend = static_cast<RHI::DX12::D3D12Backend*>(info->UserData);
            backend->allocate_imgui_srv_descriptor(*outCpuHandle, *outGpuHandle);
        }

        static void free_srv_descriptor(ImGui_ImplDX12_InitInfo* info,
                                        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle)
        {
            auto* backend = static_cast<RHI::DX12::D3D12Backend*>(info->UserData);
            backend->free_imgui_srv_descriptor(cpuHandle, gpuHandle);
        }

        static void draw_fps_window()
        {
            const ImGuiIO& io = ImGui::GetIO();
            const float fps = io.Framerate;
            const float frameTimeMs = fps > 0.0f ? 1000.0f / fps : 0.0f;

            constexpr float k_windowMargin = 16.0f;
            ImGui::SetNextWindowPos(
                ImVec2(io.DisplaySize.x - k_windowMargin, k_windowMargin),
                ImGuiCond_Always, ImVec2(1.0f, 0.0f));
            ImGui::SetNextWindowBgAlpha(0.88f);

            constexpr ImGuiWindowFlags k_windowFlags =
                ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoNav;

            if (ImGui::Begin("Frame Rate", nullptr, k_windowFlags))
            {
                ImGui::SetWindowFontScale(2.4f);
                ImGui::Text("%.1f FPS", fps);
                ImGui::SetWindowFontScale(1.35f);
                ImGui::Text("%.3f ms", frameTimeMs);

                ImGui::Separator();
                ImGui::SetWindowFontScale(1.55f);
                ImGui::TextUnformatted("CONTROLLER");
                ImGui::SetWindowFontScale(1.4f);
                ImGui::TextUnformatted("Left Stick  : Move");
                ImGui::TextUnformatted("Right Stick : Look");
                ImGui::TextUnformatted("RT / LT     : Up / Down");
                ImGui::TextUnformatted("LB          : Fast Move");
                ImGui::SetWindowFontScale(1.0f);
            }
            ImGui::End();
        }

        void draw_overlay()
        {
            draw_fps_window();

            bool directionalLightEnabled =
                m_engine.directional_light_enabled();

            ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(520.0f, 520.0f), ImGuiCond_FirstUseEver);
            ImGui::Begin("CueEngineRef GPU Driven Demo");

            if (ImGui::CollapsingHeader("Toggles", ImGuiTreeNodeFlags_DefaultOpen))
            {
                static constexpr const char* k_modeLabels[] = {
                    "Before optimization",
                    "GPU Culling",
                    "LOD",
                    "Hi-Z",
                    "Batching",
                    "Final",
                };
                static constexpr const char* k_modeDescriptions[] = {
                    "CPU-style draw commands / no culling",
                    "Reduce objects outside the view",
                    "Reduce submitted vertices",
                    "Reduce objects hidden behind occluders",
                    "Reduce draw commands",
                    "All features ON",
                };

                int comparisonMode =
                    static_cast<int>(m_engine.render_comparison_mode());
                if (ImGui::Combo("Optimization Compare", &comparisonMode,
                                 k_modeLabels, IM_ARRAYSIZE(k_modeLabels)))
                {
                    m_engine.set_render_comparison_mode(
                        static_cast<DrawSystem::RenderComparisonMode>(
                            comparisonMode));
                }

                const DrawSystem::RenderFeatureSettings& featureSettings =
                    m_engine.render_feature_settings();
                ImGui::Text("Current: %s",
                            k_modeDescriptions[comparisonMode]);
                ImGui::Text("GPU Culling: %s",
                            featureSettings.gpuCullingEnabled ? "ON" : "OFF");
                ImGui::Text("LOD: %s",
                            featureSettings.lodEnabled ? "ON" : "OFF");
                ImGui::Text("Hi-Z: %s",
                            featureSettings.hiZEnabled ? "ON" : "OFF");
                ImGui::Text("Batching: %s",
                            featureSettings.batchingEnabled ? "ON" : "OFF");

                DrawSystem::RenderDebugViewMode debugViewMode =
                    m_engine.render_debug_view_mode();
                auto debug_view_toggle =
                    [&](const char* label,
                        DrawSystem::RenderDebugViewMode mode)
                {
                    bool enabled = debugViewMode == mode;
                    if (ImGui::Checkbox(label, &enabled))
                    {
                        debugViewMode =
                            enabled ? mode
                                    : DrawSystem::RenderDebugViewMode::None;
                        m_engine.set_render_debug_view_mode(debugViewMode);
                    }
                };

                ImGui::SeparatorText("Debug Visualization");
                debug_view_toggle(
                    "LOD color",
                    DrawSystem::RenderDebugViewMode::LodColor);
                debug_view_toggle(
                    "Culling visualization",
                    DrawSystem::RenderDebugViewMode::Culling);
                debug_view_toggle(
                    "Depth Buffer",
                    DrawSystem::RenderDebugViewMode::DepthBuffer);
                debug_view_toggle(
                    "Hi-Z Mip / Tile",
                    DrawSystem::RenderDebugViewMode::HiZ);
                debug_view_toggle(
                    "Occluder Proxy compare",
                    DrawSystem::RenderDebugViewMode::OccluderProxy);
                debug_view_toggle(
                    "Clustered Lighting color",
                    DrawSystem::RenderDebugViewMode::ClusteredLighting);
                ImGui::Text("View: %s",
                            DrawSystem::render_debug_view_mode_label(
                                debugViewMode));

                if (ImGui::Checkbox("Directional Light", &directionalLightEnabled))
                {
                    m_engine.set_directional_light_enabled(directionalLightEnabled);
                }
                ImGui::TextDisabled(
                    "Lighting, scene, material, resolution, and camera stay shared.");
            }

            if (ImGui::CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::BulletText("W/A/S/D: move camera");
                ImGui::BulletText("Space / Ctrl: move up / down");
                ImGui::BulletText("Shift: fast movement");
                ImGui::BulletText("Right mouse drag: look around");
                ImGui::BulletText("Mouse over this window: operate ImGui");
            }

            ImGui::End();
        }

        RHI::DX12::D3D12Backend& m_backend;
        Engine& m_engine;
        Core::IO::IFileSystem& m_fileSystem;
        std::vector<std::byte> m_primaryFontData{};
        std::vector<std::byte> m_fallbackFontData{};
        RHI::TextureHandle m_backBuffer{};
        RHI::ViewHandle m_backBufferRtv{};
        bool m_initialized = false;
    };

} // namespace

// windows アプリのエントリーポイント
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    // パラメーター
    uint32_t width = 1920;
    uint32_t height = 1080;
    const char* className = "CueRendererWindowClass";
    const char* title = "Cue Renderer";
    uint32_t maxFps = 0;
    uint32_t bufferCount = 3;
    const Math::uint3 modelGridCount(100u, 2u, 100u);
    const float modelTargetRadius = 0.6f;
    const uint32_t maxPointLightCount = 5000;
    const bool enableDirectionalLight = false;

    Result r = Result::ok();

    std::unique_ptr<PAL::Win::WinPlatform> platform =
        std::make_unique<PAL::Win::WinPlatform>();
    std::unique_ptr<Core::CQRS::Bridge> commandBridge =
        std::make_unique<Core::CQRS::Bridge>();
    platform->set_command_bridge(commandBridge.get());
    PAL::PlatformSetupInfo setupInfo{};
    setupInfo.width = width;
    setupInfo.height = height;
    setupInfo.className = className;
    setupInfo.title = title;
    r = platform->initialize(setupInfo);

    if (!r)
    {
        CUE_ASSERT_FORMAT(false, "Failed to initialize platform: %s",
                          r.message.data());
        Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                      "Failed to initialize platform: %s", r.message.data());
        return -1;
    }

    // Renderer 起動直後の失敗も同じログ出力先へ集約する
    Core::IO::set_log_file(platform->file_system(),
                           Core::IO::Path("logs/renderer.log"), true);

    r = platform->start();
    if (!r)
    {
        CUE_ASSERT_FORMAT(false, "Failed to start platform: %s", r.message.data());
        Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
            "Failed to start platform: %s", r.message.data());
        return -1;
    }

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
    renderBackend->set_win_platform(platform.get());
    r = renderBackend->initialize(renderBackendSetupInfo);

    if (!r)
    {
        CUE_ASSERT_FORMAT(false, "Failed to initialize render backend: %s",
                          r.message.data());
        Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                      "Failed to initialize render backend: %s", r.message.data());
        return -1;
    }

    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    std::unique_ptr<RHI::FrameGraphPass> imguiOverlayPass =
        std::make_unique<ImGuiOverlayPass>(platform->get_window_handle(),
                                           *renderBackend, *engine,
                                           platform->file_system());
    platform->set_message_handler([](HWND hwnd, UINT message, WPARAM wParam,
                                     LPARAM lParam, LRESULT& outResult) -> bool
                                  {
    outResult = ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam);
    return outResult != 0; });

    EngineSetupInfo engineSetupInfo{};
    engineSetupInfo.maxFps = maxFps;
    engineSetupInfo.maxPointLightCount = maxPointLightCount;
    engineSetupInfo.enableDirectionalLight = enableDirectionalLight;
    engineSetupInfo.rendererPass = std::move(imguiOverlayPass);
    engineSetupInfo.platform = platform.get();
    engineSetupInfo.platformCommandBridge = commandBridge.get();
    engineSetupInfo.renderBackend = renderBackend.get();
    r = engine->initialize(engineSetupInfo);
    if (!r)
    {
        CUE_ASSERT_FORMAT(false, "Failed to initialize engine: %s",
                          r.message.data());
        Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                      "Failed to initialize engine: %s", r.message.data());
        return -1;
    }

    Renderer::DebugCamera debugCamera{};
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
        const char* fileName = nullptr;
        const char* modelName = nullptr;
        uint32_t lodGroupIndex = 0;
    };

    constexpr std::array<Renderer::ModelImporter::LodGroupSettings, 4>
        k_lodGroups = {
            Renderer::ModelImporter::LodGroupSettings{
                "Dragon", { 0.50f, 0.15f, 0.01f }, true },
            Renderer::ModelImporter::LodGroupSettings{
                "DragonHighPoly", { 0.25f, 0.05f, 0.005f }, true },
            Renderer::ModelImporter::LodGroupSettings{
                "Bunny", { 0.50f, 0.15f, 0.01f }, true },
            Renderer::ModelImporter::LodGroupSettings{
                "Buddha", { 0.20f, 0.03f, 0.003f }, true },
        };

    constexpr std::array<TestModelDesc, 4> k_testModels = {
        TestModelDesc{ "asiandragon.obj", "asiandragon", 0u },
        TestModelDesc{ "stanforddragon.obj", "stanforddragon", 1u },
        TestModelDesc{ "bunny.obj", "bunny", 2u },
        TestModelDesc{ "buddha.obj", "buddha", 3u },
    };

    std::vector<Core::Native::ModelData> modelDataList{};
    modelDataList.reserve(k_testModels.size());
    for (const TestModelDesc& modelDesc : k_testModels)
    {
        Core::Native::ModelData modelData{};
        const Core::IO::Path modelPath = content_path(
            platform->file_system(),
            std::string("TestProject/Assets/Models/") + modelDesc.fileName);
        if (modelDesc.lodGroupIndex >= k_lodGroups.size())
        {
            CUE_ASSERT_FORMAT(false, "Invalid LOD group index for model '%s'.",
                              modelDesc.modelName);
            return -1;
        }

        r = Renderer::ModelImporter::import_model(
            platform->file_system(), modelPath, modelDesc.modelName,
            k_lodGroups[modelDesc.lodGroupIndex], modelData);
        if (!r)
        {
            CUE_ASSERT_FORMAT(false, "Failed to import model '%s': %s",
                              modelDesc.modelName, r.message.data());
            Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                          "Failed to import model '{}': {}", modelDesc.modelName,
                          r.message);
            return -1;
        }
        modelDataList.push_back(std::move(modelData));
    }

    r = engine->register_models(modelDataList, modelGridCount, modelTargetRadius);
    if (!r)
    {
        CUE_ASSERT_FORMAT(false, "Failed to register test models: %s",
                          r.message.data());
        Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                      "Failed to register test models: %s", r.message.data());
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
        Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
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
                      gpuMemoryUsage.availableForReservationBytes / (1024 * 1024),
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

        PAL::PlatformMessage message = platform->poll_message();
        if (message == PAL::PlatformMessage::Quit)
        {
            isRunning = false;
        }

        r = platform->begin_frame();

        if (!r)
        {
            CUE_ASSERT_FORMAT(false, "Failed to begin frame: %s", r.message.data());
            return -1;
        }

        r = engine->begin_frame();

        if (!r)
        {
            CUE_ASSERT_FORMAT(false, "Failed to begin engine frame: %s",
                              r.message.data());
            return -1;
        }

        debugCamera.update(
            make_debug_camera_input(platform->get_window_handle(), deltaSeconds));
        r = engine->set_view_projection(
            debugCamera.make_view_projection(width, height));
        if (!r)
        {
            CUE_ASSERT_FORMAT(false, "Failed to update debug camera: %s",
                              r.message.data());
            return -1;
        }

        // DebugCamera の最新行列をアップロードしてからフレーム処理を進める
        r = engine->tick();

        if (!r)
        {
            CUE_ASSERT_FORMAT(false, "Failed to tick engine: %s", r.message.data());
            return -1;
        }

        r = engine->end_frame();

        if (!r)
        {
            CUE_ASSERT_FORMAT(false, "Failed to end engine frame: %s",
                              r.message.data());
            return -1;
        }

        r = platform->end_frame();

        if (!r)
        {
            CUE_ASSERT_FORMAT(false, "Failed to end frame: %s", r.message.data());
            return -1;
        }
    }

    Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                  "Renderer shutdown");
    Core::IO::clear_log_file();

    engine->shutdown();
    engine.reset();
    renderBackend->shutdown();
    renderBackend.reset();
    platform->shutdown();
    platform.reset();

    return 0;
}
