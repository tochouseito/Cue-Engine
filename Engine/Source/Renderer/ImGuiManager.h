#pragma once

/// ************************************************************************************
/// ImGuiマネージャー
/// ************************************************************************************

// === Base includes ===
#include <CueResult.h>
#include <CueAssert.h>

// === Core includes ===
#include <IO/IFileSystem.h>
#include <IO/Logger.h>

// === Win includes ===
#include <Win/win_platform.h>

// === RHI includes ===
#include <FrameGraph.h>

// === D3D12 includes ===
#include <D3D12Backend.h>

// === C++ includes ===
#include <atomic>
#include <string>

// === imgui includes ===
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>

// ImGui のウィンドウプロシージャハンドラー
extern IMGUI_IMPL_API LRESULT
ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace Cue::Editor
{
    /// @brief ImGui のセットアップに必要な情報
    struct ImGuiSetupInfo final
    {
        ImGuiSetupInfo(const uint32_t& a_bufferCount)
            : bufferCount(a_bufferCount)
        {}
        const uint32_t& bufferCount;
        HWND hwnd = nullptr;
        ID3D12Device* device = nullptr;
        ID3D12CommandQueue* commandQueue = nullptr;
        DXGI_FORMAT rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        ID3D12DescriptorHeap* srvDescHeap = nullptr;
        RHI::DX12::D3D12Backend* renderBackend = nullptr;
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

    /// @brief ImGui 描画用のフレームグラフパス
    class ImGuiPass final : public RHI::FrameGraphPass
    {
    public:
        ImGuiPass(ImGuiManager& imguiManager) : m_imguiManager(imguiManager) {}
        ~ImGuiPass() override = default;
        const char* name() const noexcept override { return "ImGuiPass"; }
        RHI::CommandListType type() const noexcept override { return RHI::CommandListType::Graphics; }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            // スワップチェインのバックバッファをフレームグラフに宣言する
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

            // GameView が参照する SRV の存在を確認する
            result = builder.get_view("FinalColorSRV", m_finalColorSrvHandle);
            if (!result)
            {
                return Result::fail(
                    Code::GetFailed,
                    Severity::Error,
                    "Failed to get final color SRV view handle for present pass.");
            }
            result = builder.get_texture("FinalColor", m_finalColorHandle);
            if (!result)
            {
                return Result::fail(
                    Code::GetFailed,
                    Severity::Error,
                    "Failed to get final color texture handle for present pass.");
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "ScreenCopyRootSignature";
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{ RHI::RootParameterType::DescriptorTableSRV, RHI::ShaderVisibility::Pixel, 0 });
            result = builder.create_root_signature(rootSignatureDesc, m_screenCopyRootSignatureHandle);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to create root signature for screen copy pass.");
            }

            const std::string shaderFilePath = "Shaders/D3D12/ScreenCopy.hlsl";
            if (shaderFilePath.empty())
            {
                return Result::fail(
                    Code::NotFound,
                    Severity::Error,
                    "Failed to locate ScreenCopy.hlsl for present pass.");
            }

            RHI::ShaderCompileDesc vertexShaderDesc{};
            vertexShaderDesc.name = "ScreenCopyVS";
            vertexShaderDesc.filePath = shaderFilePath;
            vertexShaderDesc.entryPoint = "vs_main";
            vertexShaderDesc.targetProfile = "vs_6_0";
            result = builder.create_shader_blob(vertexShaderDesc, m_screenCopyVsHandle);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to compile screen copy vertex shader.");
            }

            RHI::ShaderCompileDesc pixelShaderDesc{};
            pixelShaderDesc.name = "ScreenCopyPS";
            pixelShaderDesc.filePath = shaderFilePath;
            pixelShaderDesc.entryPoint = "ps_main";
            pixelShaderDesc.targetProfile = "ps_6_0";
            result = builder.create_shader_blob(pixelShaderDesc, m_screenCopyPsHandle);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to compile screen copy pixel shader.");
            }

            RHI::GraphicsPipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "ScreenCopyPipeline";
            pipelineDesc.rootSignatureHandle = m_screenCopyRootSignatureHandle;
            pipelineDesc.vsHandle = m_screenCopyVsHandle;
            pipelineDesc.psHandle = m_screenCopyPsHandle;
            pipelineDesc.rasterizerState.cullMode = RHI::CullMode::None;
            pipelineDesc.depthStencilState.depthEnable = false;
            pipelineDesc.depthStencilState.depthWriteMask = RHI::DepthWriteMask::Zero;
            pipelineDesc.blendMode = { RHI::BlendMode::None };
            pipelineDesc.rtvFormats = { RHI::ColorFormat::R8G8B8A8_UNORM };
            result = builder.create_graphics_pipeline(pipelineDesc, m_screenCopyPipelineHandle);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to create graphics pipeline for screen copy pass.");
            }

            return Result::ok();
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.use_texture(
                m_backBufferHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::RenderTarget,
                RHI::ResourceState::Present);
            if (!result)
            {
                return result;
            }

            return builder.use_texture(
                m_finalColorHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            RHI::ICommandContext* commandContext = context.commandContext();

            // バックバッファをクリアしておくこれがないと imgui の一部が描画されないことがある
            commandContext->clear_render_target(m_backBufferRtvHandle, k_swapChainClearColor.data());
            commandContext->set_render_targets(&m_backBufferRtvHandle, 1, {});
            commandContext->set_viewport_scissor(context.width(), context.height());
            commandContext->set_graphics_pipeline(m_screenCopyPipelineHandle);
            commandContext->set_primitive_topology(RHI::PrimitiveTopologyType::Triangle);
            commandContext->set_graphics_descriptor_table(0, m_finalColorSrvHandle);
            commandContext->draw_instanced(3, 1, 0, 0);

            // - native command list へ imgui 描画を流す
            void* nativeCommandList = commandContext->native_command_list();
            ID3D12GraphicsCommandList* dxCommandList = reinterpret_cast<ID3D12GraphicsCommandList*>(nativeCommandList);
            m_imguiManager.render(dxCommandList); // imgui 描画コマンド発行

        }
    private:
        static constexpr Math::float4 k_finalColorClearColor = Math::float4::from_rgba8(63, 63, 63, 255);
        static constexpr Math::float4 k_swapChainClearColor = Math::float4::from_rgba8(63, 63, 63, 255);
        ImGuiManager& m_imguiManager;
        RHI::TextureHandle m_backBufferHandle{};
        RHI::TextureHandle m_finalColorHandle{};
        RHI::ViewHandle m_backBufferRtvHandle{};
        RHI::ViewHandle m_finalColorSrvHandle{};
        RHI::RootSignatureHandle m_screenCopyRootSignatureHandle; // ScreenCopy 用ルートシグネチャのハンドル
        RHI::ShaderBlobHandle m_screenCopyVsHandle; // ScreenCopy の VS シェーダーハンドル
        RHI::ShaderBlobHandle m_screenCopyPsHandle; // ScreenCopy の PS シェーダーハンドル
        RHI::PipelineStateHandle m_screenCopyPipelineHandle; // ScreenCopy のグラフィックスパイプラインハンドル
    };
}
