#include "ImGuiManager.h"

namespace Cue::Editor
{
    namespace
    {
        std::string resolve_layout_file_path(Core::IO::IFileSystem& a_fileSystem)
        {
            constexpr const char* k_fallbackPath = "config/editor/imgui.ini";
            constexpr DWORD k_modulePathCapacity = 4096;

            std::string modulePath(k_modulePathCapacity, '\0');
            const DWORD length =
                ::GetModuleFileNameA(nullptr, modulePath.data(), k_modulePathCapacity);
            if (length == 0 || length >= k_modulePathCapacity)
            {
                return k_fallbackPath;
            }

            modulePath.resize(length);

            const Core::IO::Path exePath(modulePath);
            const Core::IO::Path configDirectory =
                Core::IO::Path::join(
                    exePath.parent(),
                    Core::IO::Path("config/editor"));

            const Result createResult =
                a_fileSystem.create_directories(configDirectory);
            if (!createResult)
            {
                Core::IO::log(
                    Core::IO::LogSink::debugConsole,
                    "Failed to create ImGui config directory: %s",
                    configDirectory.utf8().c_str());
            }

            return Core::IO::Path::join(
                configDirectory,
                Core::IO::Path("imgui.ini")).utf8();
        }
    }

    ImGuiManager::ImGuiManager(const ImGuiSetupInfo& a_info)
    {
        // imgui バージョン確認
        IMGUI_CHECKVERSION();
        std::string version = ImGui::GetVersion();
        Core::IO::log(Core::IO::LogSink::debugConsole, "ImGui version: {}", version.c_str());

        // imgui コンテキストの作成
        ImGui::CreateContext();

        // オプションの設定
        CUE_ASSERT(a_info.fileSystem != nullptr);
        m_layoutFilePath = resolve_layout_file_path(*a_info.fileSystem);
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = m_layoutFilePath.c_str(); // レイアウト保存先設定
        io.ConfigFlags |= a_info.enableDocking ? ImGuiConfigFlags_DockingEnable : 0;
        io.ConfigFlags |= a_info.enableMultiViewport ? ImGuiConfigFlags_ViewportsEnable : 0;
        io.ConfigFlags |= a_info.enableKeyboardNavigation ? ImGuiConfigFlags_NavEnableKeyboard : 0;

        // スタイルの設定
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.TreeLinesFlags = ImGuiTreeNodeFlags_DrawLinesFull; // ツリーノード線描画

        // プラットフォーム/レンダラーの初期化
        ImGui_ImplWin32_Init(a_info.hwnd);

        ImGui_ImplDX12_InitInfo initInfo = {};
        initInfo.Device = a_info.device;
        initInfo.CommandQueue = a_info.commandQueue;
        initInfo.NumFramesInFlight = a_info.bufferCount;
        initInfo.RTVFormat = a_info.rtvFormat;
        initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN; // dsv フォーマット未使用値設定
        initInfo.SrvDescriptorHeap = a_info.srvDescHeap;
        initInfo.LegacySingleSrvCpuDescriptor = a_info.fontSrvCpuDescHandle;
        initInfo.LegacySingleSrvGpuDescriptor = a_info.fontSrvGpuDescHandle;
        ImGui_ImplDX12_Init(&initInfo);
        m_isInitialized = true;
    }
    ImGuiManager::~ImGuiManager()
    {
        shutdown();
    }
    void ImGuiManager::shutdown()
    {
        if (!m_isInitialized)
        {
            return;
        }

        m_isBeginFrameCalled = false;

        // プラットフォーム/レンダラーのシャットダウン
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();

        // imgui コンテキスト破棄
        ImGui::DestroyContext();
        m_isInitialized = false;
    }
    Result ImGuiManager::begin_frame()
    {
        // すでにフレームが開始されているならエラー
        if (m_isBeginFrameCalled)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "Frame already begun. Call end_frame() before beginning a new frame.");
        }

        // フレーム開始
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        m_isBeginFrameCalled = true;
        return Result::ok();
    }
    Result ImGuiManager::end_frame()
    {
        // フレームが開始されていないならエラー
        if (!m_isBeginFrameCalled)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "Frame not begun. Call begin_frame() before ending the frame.");
        }

        // フレーム終了
        ImGui::EndFrame();
        return Result::ok();
    }
    Result ImGuiManager::render(ID3D12GraphicsCommandList* commandList)
    {
        // フレームが開始されていないならエラー
        if (!m_isBeginFrameCalled)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "Frame not begun. Call begin_frame() before rendering the frame.");
        }

        // 描画コマンドを生成
        ImGui::Render();
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        // 描画データ取得
        ImDrawData* drawData = ImGui::GetDrawData();
        if (drawData != nullptr && drawData->CmdListsCount != 0)
        {
            ImGui_ImplDX12_RenderDrawData(drawData, commandList);
        }
        m_isBeginFrameCalled = false; // フレーム終了状態へ更新

        return Result::ok();
    }
}
