#pragma once

// === Base includes ===
#include <Result.h>
#include <CueAssert.h>

// === Core includes ===
#include <IO/Logger.h>

// === Win includes ===
#include <win/win_platform.h>

// === d3d12 includes ===
#include <d3d12_backend.h>

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
        D3D12_CPU_DESCRIPTOR_HANDLE fontSrvCpuDescHandle = {};
        D3D12_GPU_DESCRIPTOR_HANDLE fontSrvGpuDescHandle = {};
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
    private:
        std::atomic_bool m_isBeginFrameCalled = false;
        bool m_isInitialized = false;
        const char* m_layoutFilePath = "config/imgui_layout.ini";
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

            return Result::ok();
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            RHI::ICommandContext* commandContext = context.commandContext();

            {
                RHI::ResourceBarrierDesc barrierDesc{};
                barrierDesc.before = RHI::ResourceState::Present;
                barrierDesc.after = RHI::ResourceState::RenderTarget;
                commandContext->resource_barrier(m_backBufferHandle, barrierDesc);
            }

            // バックバッファをクリアしておく。これがないと imgui の一部が描画されないことがある。
            commandContext->clear_render_target(m_backBufferRtvHandle, k_swapChainClearColor.data());

            // 3) native command list へ imgui 描画を流す
            void* nativeCommandList = commandContext->native_command_list();
            ID3D12GraphicsCommandList* dxCommandList = reinterpret_cast<ID3D12GraphicsCommandList*>(nativeCommandList);
            m_imguiManager.render(dxCommandList); // imgui 描画コマンド発行

            // バックバッファをプレゼント状態に戻す。
            {
                RHI::ResourceBarrierDesc barrierDesc{};
                barrierDesc.before = RHI::ResourceState::RenderTarget;
                barrierDesc.after = RHI::ResourceState::Present;
                commandContext->resource_barrier(m_backBufferHandle, barrierDesc);
            }
        }
    private:
        static constexpr std::array<float, 4> k_swapChainClearColor = { 0.5f, 0.0f, 0.0f, 1.0f };
        ImGuiManager& m_imguiManager;
        RHI::TextureHandle m_backBufferHandle{};
        RHI::ViewHandle m_backBufferRtvHandle{};
    };
}
