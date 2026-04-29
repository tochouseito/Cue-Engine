#pragma once

// === Base includes ===
#include <Result.h>
#include <CueAssert.h>

// === Core includes ===
#include <IO/IFileSystem.h>
#include <IO/Logger.h>

// === Win includes ===
#include <WinPlatform.h>

// === D3D12 includes ===
#include <D3D12Backend.h>

// === C++ includes ===
#include <atomic>
#include <string>

// === imgui includes ===
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>
extern IMGUI_IMPL_API LRESULT
ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace Cue::Editor
{
    struct ImGuiSetupInfo final
    {
        ImGuiSetupInfo(const uint32_t& a_bufferCount)
            : bufferCount(a_bufferCount)
        {
        }
        const uint32_t& bufferCount;
        HWND hwnd = nullptr;
        ID3D12Device* device = nullptr;
        ID3D12CommandQueue* commandQueue = nullptr;
        DXGI_FORMAT rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        ID3D12DescriptorHeap* srvDescHeap = nullptr;
        RHI::DX12::D3D12Backend* backend = nullptr;
        Core::IO::IFileSystem* fileSystem = nullptr;
        bool enableDocking = true;
        bool enableMultiViewport = false;
        bool enableKeyboardNavigation = true;
    };

    class ImGuiManager final
    {
    public:
        ImGuiManager(const ImGuiSetupInfo& a_info);
        ~ImGuiManager();

        void shutdown();
        Result begin_frame();
        Result end_frame();
        Result render(ID3D12GraphicsCommandList* commandList);
        [[nodiscard]] ImFont* ui_font() const noexcept
        {
            return m_uiFont;
        }
        [[nodiscard]] ImFont* code_font() const noexcept
        {
            return m_codeFont;
        }
    private:
        std::atomic_bool m_isBeginFrameCalled = false;
        bool m_isInitialized = false;
        std::string m_layoutFilePath{};
        ImFont* m_uiFont = nullptr;
        ImFont* m_codeFont = nullptr;
    };

    class ImGuiPass final : public RHI::FrameGraphPass
    {
    public:
        ImGuiPass(ImGuiManager& imguiManager) : m_imguiManager(imguiManager) {}
        ~ImGuiPass() override = default;
        const char* name() const noexcept override { return "ImGuiPass"; }
        RHI::CommandListType type() const noexcept override { return RHI::CommandListType::Graphics; }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            // スワップチェインのバックバッファをフレームグラフに宣言する。
            Result result = builder.get_texture("BackBuffer", m_backBufferHandle);
            if (!result)
            {
                return Result::fail(
                    Code::GetFailed,
                    Severity::Error,
                    "Failed to get back buffer texture handle for present pass.");
            }
            result = builder.render(&m_backBufferHandle, 1);
            if (!result)
            {
                return Result::fail(
                    Code::GetFailed,
                    Severity::Error,
                    "Failed to declare back buffer as render target for present pass.");
            }
            result = builder.get_view("BackBufferRTV", m_backBufferRtvHandle);
            if (!result)
            {
                return Result::fail(
                    Code::GetFailed,
                    Severity::Error,
                    "Failed to get back buffer RTV view handle for present pass.");
            }

            // GameView が参照する SRV の存在を確認する。
            result = builder.get_view("GameColorSRV", m_gameColorSrvHandle);
            if (!result)
            {
                return Result::fail(
                    Code::GetFailed,
                    Severity::Error,
                    "Failed to get game color SRV view handle for present pass.");
            }

            return Result::ok();
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            return builder.use_texture(
                m_backBufferHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::RenderTarget,
                RHI::ResourceState::Present);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            RHI::ICommandContext* commandContext = context.commandContext();

            // バックバッファをクリアしておく。これがないと imgui の一部が描画されないことがある。
            commandContext->clear_render_target(m_backBufferRtvHandle, k_swapChainClearColor.data());
            commandContext->set_render_targets(&m_backBufferRtvHandle, 1, {});
            commandContext->set_viewport_scissor(context.width(), context.height());

            // 3) native command list へ imgui 描画を流す
            void* nativeCommandList = commandContext->native_command_list();
            ID3D12GraphicsCommandList* dxCommandList = reinterpret_cast<ID3D12GraphicsCommandList*>(nativeCommandList);
            m_imguiManager.render(dxCommandList); // imgui 描画コマンド発行

        }
    private:
        static constexpr Math::float4 k_finalColorClearColor = Math::float4::from_rgba8(63, 63, 63, 255);
        static constexpr Math::float4 k_swapChainClearColor = Math::float4::from_rgba8(63, 63, 63, 255);
        ImGuiManager& m_imguiManager;
        RHI::TextureHandle m_backBufferHandle{};
        RHI::ViewHandle m_backBufferRtvHandle{};
        RHI::ViewHandle m_gameColorSrvHandle{};
    };
}
