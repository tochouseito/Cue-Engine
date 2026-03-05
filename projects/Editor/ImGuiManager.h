#pragma once

// Base includes
#include <Result.h>
#include <CueAssert.h>

// Core includes
#include <Logger.h>

// GraphicsCore includes
#include <FrameGraph.h>

// DX12 includes


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
        ID3D12CommandQueue* commandQueue = nullptr;
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

            ImGui_ImplDX12_InitInfo initInfo = {};
            initInfo.Device = setupInfo.device;
            initInfo.CommandQueue = setupInfo.commandQueue;
            initInfo.NumFramesInFlight = setupInfo.bufferingCount;
            initInfo.RTVFormat = setupInfo.rtvFormat;
            initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN; // 深度バッファは使用しないため、DSVフォーマットは指定しない
            initInfo.LegacySingleSrvCpuDescriptor = setupInfo.fontSrvCpuDescHandle;
            initInfo.LegacySingleSrvGpuDescriptor = setupInfo.fontSrvGpuDescHandle;
            ImGui_ImplDX12_Init(&initInfo);

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
            // 2) すでにフレームが開始されているならエラー
            if(m_isBeginFrameCalled)
            {
                return Result::fail(
                    Facility::Core, Code::InvalidState, Severity::Error, 0,
                    "ImGui frame is already begun");
            }
            // 3) フレーム開始
            ImGui_ImplDX12_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            m_isBeginFrameCalled = true;
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
            // 2) フレームが開始されていないならエラー
            if (!m_isBeginFrameCalled)
            {
                return Result::fail(
                    Facility::Core, Code::InvalidState, Severity::Error, 0,
                    "ImGui frame is not begun");
            }
            // 3) フレーム終了
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
            // 2) フレームが開始されていないならエラー
            if(!m_isBeginFrameCalled)
            {
                return Result::fail(
                    Facility::Core, Code::InvalidState, Severity::Error, 0,
                    "ImGui frame is not begun");
            }
            // 3) 描画コマンドを生成
            ImGui::Render();
            ImGuiIO& io = ImGui::GetIO();
            if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }

            // 4) 有効な描画データを取得
            ImDrawData* drawData = ImGui::GetDrawData();
            if (drawData != nullptr || drawData->CmdListsCount != 0)
            {
                ImGui_ImplDX12_RenderDrawData(drawData, commandList);
            }
            m_isBeginFrameCalled = false; // フレームが終了したので、次のフレームを開始できるようにする
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
        std::atomic_bool m_isBeginFrameCalled = false;
        const char* m_layoutFilePath = "config/imgui_layout.ini";
    };

    class ImGuiPass final : public GraphicsCore::FrameGraphPass
    {
    public:
        ImGuiPass(ImGuiManager& imguiManager)
            : m_imguiManager(imguiManager)
        {
        }
        ~ImGuiPass() override = default;
        [[nodiscard]] const char* name() const override
        {
            return "ImGuiPass";
        }

        void setup(GraphicsCore::FrameGraphBuilder& builder) override
        {
            // 1) ImGuiの描画はフレームグラフの最後に行うため、常にバックバッファにレンダリングするパスを宣言する。
            //    これにより、ImGuiが他のパスの後で確実に描画されるようになる。
            GraphicsCore::TextureDesc desc{};
            desc.name = "SwapChain.BackBuffer";
            desc.instanceSource = GraphicsCore::ResourceInstanceSource::SwapchainImageIndex;
            m_backBuffer = builder.import_texture(desc.name, desc, GraphicsCore::ResourceState::Present);
            builder.render(m_backBuffer, GraphicsCore::ResourceState::Present);
        }

        void execute(GraphicsCore::FrameGraphContext& ctx) const override
        {
            GraphicsCore::ICommandContext& commandContext = ctx.command_context();

            GraphicsCore::TextureHandle resolvedBackBuffer{};
            const Result resolveResult = ctx.resolve_texture(m_backBuffer, resolvedBackBuffer);
            if (!resolveResult)
            {
                Core::Logger::log(Core::LogSink::debugConsole, "[ImGuiPass] failed to resolve back buffer for swapchain image {}\n", ctx.swapchain_image_index());
                return;
            }
            constexpr float clearColor[4] = { 0.07f, 0.11f, 0.18f, 1.0f };
            const Result clearResult = commandContext.clear_render_target(resolvedBackBuffer, clearColor);
            if (!clearResult)
            {
                Core::Logger::log(Core::LogSink::debugConsole, "[ImGuiPass] failed to clear back buffer for swapchain image {}\n", ctx.swapchain_image_index());
                return;
            }

            /*GraphicsCore::TextureViewDesc rtvDesc{};
            rtvDesc.type = GraphicsCore::ViewType::RenderTarget;
            GraphicsCore::ViewHandle rtvHandle{};
            const Result getViewResult = ctx.view_manager().get_texture_view(resolvedBackBuffer, rtvDesc, rtvHandle);
            if (!getViewResult)
            {
                Core::Logger::log(Core::LogSink::debugConsole, "[ImGuiPass] failed to get texture view for swapchain image {}\n", ctx.swapchain_image_index());
                return;
            }*/


            GraphicsCore::NativeCommandList nativeCommandList = commandContext.native_command_list();
            ID3D12GraphicsCommandList* dxCommandList = reinterpret_cast<ID3D12GraphicsCommandList*>(nativeCommandList);
            m_imguiManager.render(dxCommandList); // ImGuiの描画コマンドを発行する
        }
    private:
        ImGuiManager& m_imguiManager;
        GraphicsCore::TextureHandle m_backBuffer;
    };
}
