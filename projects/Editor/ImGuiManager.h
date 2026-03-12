#pragma once

// base 関連 include
#include <Result.h>
#include <CueAssert.h>

// core 関連 include
#include <Logger.h>

// graphics core 関連 include
#include <FrameGraph.h>

// dx12 関連 include


// imgui 関連 include
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>
extern IMGUI_IMPL_API LRESULT
ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// c++ 標準ライブラリ include
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
        /// @brief 生成
        ImGuiManager() = default;
        /// @brief 破棄
        ~ImGuiManager() = default;

        /// @brief 初期化
        Result initialize(const imgui_setup_info& setupInfo)
        {
            // 1) 初期化済みなら何もしない
            if (m_isInitialized)
            {
                return Result::ok();
            }

            // 2) imgui バージョン確認
            IMGUI_CHECKVERSION();
            std::string version = IMGUI_VERSION;
            Core::Logger::log(Core::LogSink::debugConsole, "Initializing ImGui (version {})", version);

            // 3) imgui コンテキスト作成
            ImGui::CreateContext();

            // 4) オプションの設定
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = m_layoutFilePath; // レイアウト保存先設定
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
            initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN; // dsv フォーマット未使用値設定
            initInfo.SrvDescriptorHeap = setupInfo.srvDescHeap;
            initInfo.LegacySingleSrvCpuDescriptor = setupInfo.fontSrvCpuDescHandle;
            initInfo.LegacySingleSrvGpuDescriptor = setupInfo.fontSrvGpuDescHandle;
            ImGui_ImplDX12_Init(&initInfo);

            // 6) スタイルの設定
            ImGui::StyleColorsDark();
            ImGuiStyle& style = ImGui::GetStyle();
            style.TreeLinesFlags = ImGuiTreeNodeFlags_DrawLinesFull; // ツリーノード線描画

            // 7) 初期化完了
            m_isInitialized = true;
            return Result::ok();
        }
        /// @brief 終了
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

            // 3) imgui コンテキスト破棄
            ImGui::DestroyContext();

            // 4) シャットダウン完了
            m_isInitialized = false;
        }
        /// @brief フレーム開始
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
            if (m_isBeginFrameCalled)
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
        /// @brief フレーム終了
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
        /// @brief 描画
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
            if (!m_isBeginFrameCalled)
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

            // 4) 描画データ取得
            ImDrawData* drawData = ImGui::GetDrawData();
            if (drawData != nullptr && drawData->CmdListsCount != 0)
            {
                ImGui_ImplDX12_RenderDrawData(drawData, commandList);
            }
            m_isBeginFrameCalled = false; // フレーム終了状態へ更新
            return Result::ok();
        }

        /// @brief レイアウト保存
        Result save_layout()
        {
            return Result::ok(); // レイアウト保存を imgui 標準動作へ委譲
        }
        /// @brief レイアウト読込
        Result load_layout()
        {
            return Result::ok(); // レイアウト読込を imgui 標準動作へ委譲
        }
    private:
        bool m_isInitialized = false;
        std::atomic_bool m_isBeginFrameCalled = false;
        const char* m_layoutFilePath = "config/imgui_layout.ini";
    };

    class ImGuiPass final : public GraphicsCore::FrameGraphPass
    {
    public:
        /// @brief pass 生成
        ImGuiPass(ImGuiManager& imguiManager)
            : m_imguiManager(imguiManager)
        {
        }
        /// @brief pass 破棄
        ~ImGuiPass() override = default;
        /// @brief pass 名取得
        [[nodiscard]] const char* name() const override
        {
            return "ImGuiPass";
        }

        /// @brief setup
        void setup(GraphicsCore::FrameGraphBuilder& builder) override
        {
            // 1) render graph が最後に書き込み完了した final color を srv 読みで宣言
            GraphicsCore::TextureDesc finalColorDesc{};
            finalColorDesc.name = "FinalColor";
            finalColorDesc.instanceSource = GraphicsCore::ResourceInstanceSource::LatestCompleted;
            m_finalColor = builder.import_texture(finalColorDesc.name, finalColorDesc, GraphicsCore::ResourceState::ShaderResource);
            builder.read(m_finalColor);

            // 2) back buffer 描画 pass を宣言
            //    imgui 描画順を後段へ固定
            GraphicsCore::TextureDesc desc{};
            desc.name = "SwapChain.BackBuffer";
            desc.instanceSource = GraphicsCore::ResourceInstanceSource::SwapchainImageIndex;
            m_backBuffer = builder.import_texture(desc.name, desc, GraphicsCore::ResourceState::Present);
            builder.render(m_backBuffer, GraphicsCore::ResourceState::Present);
        }

        /// @brief execute
        void execute(GraphicsCore::FrameGraphContext& ctx) const override
        {
            // 1) command context と back buffer を解決する
            GraphicsCore::ICommandContext& commandContext = ctx.command_context();

            GraphicsCore::TextureHandle resolvedBackBuffer{};
            const Result resolveResult = ctx.resolve_texture(m_backBuffer, resolvedBackBuffer);
            if (!resolveResult)
            {
                Core::Logger::log(Core::LogSink::debugConsole, "[BackBufferClearPass] failed to resolve back buffer for swapchain image {}\n", ctx.swapchain_image_index());
                return;
            }

            // 2) back buffer を clear する
            constexpr float clearColor[4] = { 0.07f, 0.11f, 0.18f, 1.0f };
            const Result clearResult = ctx.command_context().clear_render_target(resolvedBackBuffer, clearColor);
            if (!clearResult)
            {
                Core::Logger::log(Core::LogSink::debugConsole, "[BackBufferClearPass] failed to clear back buffer for swapchain image {}\n", ctx.swapchain_image_index());
            }

            // 3) native command list へ imgui 描画を流す
            GraphicsCore::NativeCommandList nativeCommandList = commandContext.native_command_list();
            ID3D12GraphicsCommandList* dxCommandList = reinterpret_cast<ID3D12GraphicsCommandList*>(nativeCommandList);
            m_imguiManager.render(dxCommandList); // imgui 描画コマンド発行
        }
    private:
        ImGuiManager& m_imguiManager;
        GraphicsCore::TextureHandle m_finalColor;
        GraphicsCore::TextureHandle m_backBuffer;
    };
}
