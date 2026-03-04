#pragma once

// Base includes
#include <Result.h>
#include <CueAssert.h>

// Core includes
#include <Logger.h>

// ImGui includes
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>

// C++ includes
#include <string>

namespace Cue::Editor
{
    struct imgui_setup_info
    {
        HWND hwnd = nullptr;
        ID3D12Device* device = nullptr;
        uint32_t bufferingCount = 2;
        DXGI_FORMAT rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        ID3D12DescriptorHeap* srvDescHeap = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE fontSrvCpuDescHandle = {};
        D3D12_GPU_DESCRIPTOR_HANDLE fontSrvGpuDescHandle = {};
        bool enableDocking = true;
        bool enableMultiViewport = false;
        bool enableKeyboardNavigation = true;
    };

    class ImGuiManager final
    {
    public:
        ImGuiManager() = default;
        ~ImGuiManager() = default;

        Result initialize(const imgui_setup_info& setupInfo)
        {
            // 1) 初期化済みなら何もしない
            if (m_isInitialized)
            {
                return Result::ok();
            }

            // 2) ImGuiのバージョンをチェック
            IMGUI_CHECKVERSION();
            std::string version = IMGUI_VERSION;
            Core::Logger::log(Core::LogSink::debugConsole, "Initializing ImGui (version {})", version);

            // 3) ImGuiのコンテキストを作成
            ImGui::CreateContext();

            // 4) オプションの設定
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = m_layoutFilePath; // レイアウトの保存先を指定
            io.ConfigFlags |= setupInfo.enableDocking ? ImGuiConfigFlags_DockingEnable : 0;
            io.ConfigFlags |= setupInfo.enableMultiViewport ? ImGuiConfigFlags_ViewportsEnable : 0;
            io.ConfigFlags |= setupInfo.enableKeyboardNavigation ? ImGuiConfigFlags_NavEnableKeyboard : 0;

            // 5) プラットフォーム/レンダラーの初期化
            ImGui_ImplWin32_Init(setupInfo.hwnd);
            ImGui_ImplDX12_Init(
                setupInfo.device,
                setupInfo.bufferingCount,
                setupInfo.rtvFormat,
                setupInfo.srvDescHeap,
                setupInfo.fontSrvCpuDescHandle,
                setupInfo.fontSrvGpuDescHandle);

            // 6) スタイルの設定
            ImGui::StyleColorsDark();
            ImGuiStyle& style = ImGui::GetStyle();
            style.TreeLinesFlags = ImGuiTreeNodeFlags_DrawLinesFull;// ツリーノードの線を全て描画する

            // 7) 初期化完了
            m_isInitialized = true;
            return Result::ok();
        }
        void shutdown()
        {
            // 1) 初期化されていないなら何もしない
            if (!m_isInitialized)
            {
                return;
            }

            // 2) プラットフォーム/レンダラーのシャットダウン
            ImGui_ImplDX12_Shutdown();
            ImGui_ImplWin32_Shutdown();

            // 3) ImGuiのコンテキストを破棄
            ImGui::DestroyContext();

            // 4) シャットダウン完了
            m_isInitialized = false;
        }
        Result begin_frame()
        {
            // 1) 初期化されていないならエラー
            if (!m_isInitialized)
            {
                return Result::fail(
                    Facility::Core, Code::InvalidState, Severity::Error, 0,
                    "ImGuiManager is not initialized");
            }
            // 2) フレーム開始
            ImGui_ImplDX12_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            return Result::ok();
        }
        Result end_frame()
        {
            // 1) 初期化されていないならエラー
            if (!m_isInitialized)
            {
                return Result::fail(
                    Facility::Core, Code::InvalidState, Severity::Error, 0,
                    "ImGuiManager is not initialized");
            }
            // 2) フレーム終了
            ImGui::EndFrame();
            return Result::ok();
        }
        Result render(ID3D12GraphicsCommandList* commandList)
        {
            // 1) 初期化されていないならエラー
            if (!m_isInitialized)
            {
                return Result::fail(
                    Facility::Core, Code::InvalidState, Severity::Error, 0,
                    "ImGuiManager is not initialized");
            }
            // 2) 描画コマンドを生成
            ImGui::Render();
            ImGuiIO& io = ImGui::GetIO();
            if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }

            // 3) 有効な描画データを取得
            ImDrawData* drawData = ImGui::GetDrawData();
            if (drawData == nullptr || drawData->CmdListsCount == 0)
            {
                return Result::ok(); // 描画するものがない
            }
            ImGui_ImplDX12_RenderDrawData(drawData, commandList);

            return Result::ok();
        }

        Result save_layout()
        {
            return Result::ok(); // ImGuiは自動でレイアウトを保存するため、特に何もしない
        }
        Result load_layout()
        {
            return Result::ok(); // ImGuiは自動でレイアウトを読み込むため、特に何もしない
        }
    private:
        bool m_isInitialized = false;
        const char* m_layoutFilePath = "config/imgui_layout.ini";
    };
}
