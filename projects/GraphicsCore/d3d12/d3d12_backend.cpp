#include "d3d12_backend.h"
#include <win/win_native.h>
#include "ResourceLeakChecker.h"
#include "RenderDevice.h"
#include "DX12GpuCommand.h"
#include "DX12BufferManager.h"
#include "private/DX12PipelineManager.h"
#include "private/HLSLCompiler.h"
#include "SwapChain.h"

namespace Cue::GraphicsCore
{
    std::unique_ptr<Backend> create_backend()
    {
        // 1) 現在の既定backendとしてD3D12実装を返す
        return std::make_unique<DX12::D3D12Backend>();
    }
}

namespace Cue::GraphicsCore::DX12
{
    struct D3D12Backend::Impl
    {
        // 実装の詳細をここに記述
        Platform::Win::WinPlatform* m_winPlatform = nullptr;
        std::unique_ptr<ResourceLeakChecker> m_leakChecker = std::make_unique<ResourceLeakChecker>();
        std::unique_ptr<DX12RenderDevice> m_renderDevice = std::make_unique<DX12RenderDevice>();
        std::unique_ptr<CommandPool> m_commandPool = nullptr;
        std::unique_ptr<QueuePool> m_queuePool = nullptr;
        std::unique_ptr<HLSLCompiler> m_shaderCompiler = std::make_unique<HLSLCompiler>();
        std::unique_ptr<DX12BufferManager> m_bufferManager = nullptr;
        std::unique_ptr<TextureManager> m_textureManager = nullptr;
        std::unique_ptr<DX12PipelineManager> m_pipelineManager = nullptr;
        std::unique_ptr<SwapChain> m_swapChain = nullptr;
    };

    D3D12Backend::D3D12Backend()
        : m_impl(std::make_unique<Impl>())
    {
    }
    D3D12Backend::~D3D12Backend()
    {
    }
    Result D3D12Backend::initialize(const backend_setup_info& info)
    {
        if (!m_impl->m_winPlatform)
        {
            return Result::fail(
                Facility::D3D12,
                Code::InvalidState,
                Severity::Error,
                0,
                "WinPlatform is not set in D3D12Backend.");
        }

        // 1) デバイス初期化失敗をそのまま返し、失敗点を保持する
        Result r = m_impl->m_renderDevice->initialize(true);
        if (!r)
        {
            return r;
        }

        // 2) コマンドプールとキュープールを作成する
        m_impl->m_commandPool = std::make_unique<CommandPool>(m_impl->m_renderDevice->get_d3d12_device());
        m_impl->m_queuePool = std::make_unique<QueuePool>(m_impl->m_renderDevice->get_d3d12_device());

        // 3) バッファマネージャを作成する
        m_impl->m_bufferManager = std::make_unique<DX12BufferManager>(*m_impl->m_renderDevice.get());

        m_impl->m_textureManager = std::make_unique<TextureManager>();

        // 4) スワップチェインを作成する
        m_impl->m_swapChain = std::make_unique<SwapChain>(
            *m_impl->m_renderDevice.get(),
            *m_impl->m_queuePool,
            m_impl->m_bufferManager->get_descriptor_allocator());
        r = m_impl->m_swapChain->create(
            reinterpret_cast<HWND>(m_impl->m_winPlatform->get_native_window_handle()),
            m_impl->m_winPlatform->window_width(),
            m_impl->m_winPlatform->window_height(),
            DXGI_FORMAT_R8G8B8A8_UNORM,
            info.bufferCount);

        //
        m_impl->m_pipelineManager = std::make_unique<DX12PipelineManager>(*m_impl->m_renderDevice.get(), *m_impl->m_shaderCompiler);

        //
        r = create_frame_graph_runtime(&m_frameGraphRuntime);
        m_frameGraph = std::make_unique<FrameGraph>(*m_impl->m_bufferManager, *m_impl->m_textureManager);

        // 2) すべての初期化が成功したことを返す
        return Result::ok();
    }
    Result D3D12Backend::shutdown()
    {
        m_frameGraphRuntime.reset();
        return Result::ok();
    }
    Result D3D12Backend::build_frame_graph()
    {
        return m_frameGraph->build();
    }
    Result D3D12Backend::render()
    {
       return m_frameGraph->execute(*m_frameGraphRuntime);
    }
    void D3D12Backend::set_win_platform(Platform::IPlatform* platform)
    {
        m_impl->m_winPlatform = dynamic_cast<Platform::Win::WinPlatform*>(platform);
    }
    Result D3D12Backend::create_frame_graph_runtime(std::unique_ptr<IFrameGraphRuntime>* outRuntime)
    {
        // 1) 出力先と pool の生成状態を検証し、不完全な backend から runtime を作らない。
        if (outRuntime == nullptr)
        {
            return Result::fail(
                Facility::D3D12,
                Code::InvalidArg,
                Severity::Error,
                0,
                "Output runtime pointer is null.");
        }
        if (!m_impl->m_commandPool || !m_impl->m_queuePool)
        {
            return Result::fail(
                Facility::D3D12,
                Code::InvalidState,
                Severity::Error,
                0,
                "CommandPool or QueuePool is not initialized.");
        }

        // 2) queue を固定保持する runtime を生成して初期化し、成功時だけ公開する。
        auto runtime = std::make_unique<Dx12FrameGraphRuntime>(*m_impl->m_commandPool, *m_impl->m_queuePool);
        Result result = runtime->initialize();
        if (!result)
        {
            return result;
        }

        *outRuntime = std::move(runtime);
        return Result::ok();
    }
} // namespace Cue::Graphics::DX12
