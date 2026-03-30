#include "DX12GpuCommand.h"

namespace Cue::RHI::DX12
{
    namespace
    {
        constexpr UINT k_eventMetadataAnsi = 1u;
    }

    DX12GpuCommandContext::DX12GpuCommandContext(ID3D12Device& device,
        DescriptorAllocator& descriptorAllocator,
        DX12BufferManager& bufferManager,
        DX12TextureManager& textureManager,
        DX12ViewManager& viewManager,
        DX12PipelineManager& pipelineManager,
        D3D12_COMMAND_LIST_TYPE type)
        : m_descriptorAllocator(descriptorAllocator),
          m_bufferManager(bufferManager),
          m_textureManager(textureManager),
          m_viewManager(viewManager),
          m_pipelineManager(pipelineManager)
    {
        // コマンドアロケータの作成
        create_command_allocator(device, type);
        // コマンドリストの作成
        create_command_list(device, type);

        m_type = convert_command_list_type(type);
    }
    Result DX12GpuCommandContext::setup(uint32_t frameIndex)
    {
        // copy command list は descriptor heap を扱えないため、setup は no-op で返す。
        if (type() == CommandListType::Copy)
        {
            return Result::ok();
        }

        m_frameIndex = frameIndex;
        auto srvHeap = m_descriptorAllocator.get_descriptor_heap(HeapType::CBV_SRV_UAV);
        m_commandList->SetDescriptorHeaps(1, &srvHeap);
        return Result::ok();
    }
    Result DX12GpuCommandContext::reset()
    {
        if (!m_commandAllocator || !m_commandList)
        {
            return Result::fail(
                Code::InternalError,
                Severity::Fatal,
                "CommandAllocator or CommandList is not initialized.");
        }

        // コマンドアロケータのリセット
        HRESULT hr = m_commandAllocator->Reset();
        if (FAILED(hr))
        {
            return Result::fail(
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                "Failed to reset CommandAllocator.");
        }

        // コマンドリストのリセット
        hr = m_commandList->Reset(
            m_commandAllocator.Get(),
            nullptr);
        if (FAILED(hr))
        {
            return Result::fail(
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                "Failed to reset CommandList.");
        }

        return Result::ok();
    }
    Result DX12GpuCommandContext::close()
    {
        if (!m_commandList)
        {
            return Result::fail(
                Code::InternalError,
                Severity::Fatal,
                "CommandList is not initialized.");
        }

        // コマンドリストのクローズ
        HRESULT hr = m_commandList->Close();
        if (FAILED(hr))
        {
            return Result::fail(
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                "Failed to close CommandList.");
        }

        return Result::ok();
    }
    CommandListType DX12GpuCommandContext::type() const
    {
        return m_type;
    }
    void DX12GpuCommandContext::begin_event(const char* name)
    {
        // コマンドリスト未初期化時はイベント記録を行えないため、何もせず戻る。
        if (m_commandList == nullptr)
        {
            return;
        }

        // 空名はデバッグ時の識別性を落とすため、既定名に置き換える。
        const char* eventName = name;
        if (eventName == nullptr || eventName[0] == '\0')
        {
            eventName = "UnnamedEvent";
        }

        // metadata と size を文字列形式に合わせて指定し、デバッグレイヤーの破損判定を回避する。
        const UINT eventNameBytes = static_cast<UINT>((std::char_traits<char>::length(eventName) + 1) * sizeof(eventName[0]));
        m_commandList->BeginEvent(k_eventMetadataAnsi, eventName, eventNameBytes);
    }
    void DX12GpuCommandContext::end_event()
    {
        // コマンドリスト未初期化時は end marker を積めないため、何もせず戻る。
        if (m_commandList == nullptr)
        {
            return;
        }

        // begin_event で積んだスコープを閉じ、GPU キャプチャ上のパス範囲を確定する。
        m_commandList->EndEvent();
    }
    Result DX12GpuCommandContext::resource_barrier(BufferHandle handle, const ResourceBarrierDesc desc)
    {
        // ハンドルからリソースを取得する
        DX12BufferRecord* record = nullptr;
        if (!m_bufferManager.try_get_record(handle, &record))
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Buffer record was not found for the given handle.");
        }
        DX12GpuResource* resource = &record->defaultResources[m_frameIndex];
        ID3D12Resource* d3dResource = resource->get_resource();
        if (d3dResource == nullptr)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "D3D12 resource was not found for the given buffer handle.");
        }

        D3D12_RESOURCE_BARRIER d3d12Barrier{};
        d3d12Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        d3d12Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        d3d12Barrier.Transition.pResource = d3dResource;
        d3d12Barrier.Transition.StateBefore = convert_resource_state(desc.before);
        d3d12Barrier.Transition.StateAfter = convert_resource_state(desc.after);
        d3d12Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        m_commandList->ResourceBarrier(1, &d3d12Barrier);

        return Result::ok();
    }
    Result DX12GpuCommandContext::resource_barrier(TextureHandle handle, const ResourceBarrierDesc desc)
    {
        // ハンドルからリソースを取得する
        DX12TextureRecord* record = nullptr;
        if (!m_textureManager.try_get_record(handle, &record))
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Texture record was not found for the given handle.");
        }
        DX12GpuResource* resource = &record->defaultResources[m_frameIndex];
        ID3D12Resource* d3dResource = resource->get_resource();
        if (d3dResource == nullptr)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "D3D12 resource was not found for the given texture handle.");
        }

        D3D12_RESOURCE_BARRIER d3d12Barrier{};
        d3d12Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        d3d12Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        d3d12Barrier.Transition.pResource = d3dResource;
        d3d12Barrier.Transition.StateBefore = convert_resource_state(desc.before);
        d3d12Barrier.Transition.StateAfter = convert_resource_state(desc.after);
        d3d12Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        m_commandList->ResourceBarrier(1, &d3d12Barrier);

        return Result::ok();
    }
    Result DX12GpuCommandContext::clear_render_target(ViewHandle handle, const float clearColor[4])
    {
        // ハンドルからビューを取得する
        DX12ViewRecord* record = nullptr;
        if (!m_viewManager.try_get_record(handle, &record))
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "View record was not found for the given handle.");
        }

        // 正しいビュータイプか確認する
        if (record->desc.type != ViewType::RenderTarget)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "The view type of the given handle is not RenderTarget.");
        }
        auto cpuHandle = m_descriptorAllocator.get_cpu_handle(record->defaultTableIds[m_frameIndex]);

        // RenderTarget のクリア
        m_commandList->ClearRenderTargetView(cpuHandle, clearColor, 0, nullptr);

        return Result::ok();
    }
    Result DX12GpuCommandContext::clear_depth_stencil(ViewHandle handle, float depth, uint8_t stencil)
    {
        // ハンドルからビューを取得する
        DX12ViewRecord* record = nullptr;
        if (!m_viewManager.try_get_record(handle, &record))
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "View record was not found for the given handle.");
        }

        // 正しいビュータイプか確認する
        if (record->desc.type != ViewType::DepthStencil)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "The view type of the given handle is not DepthStencil.");
        }

        auto cpuHandle = m_descriptorAllocator.get_cpu_handle(record->defaultTableIds[m_frameIndex]);

        // DepthStencil のクリア
        m_commandList->ClearDepthStencilView(
            cpuHandle,
            D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
            depth,
            stencil,
            0,
            nullptr);

        return Result::ok();
    }
    Result DX12GpuCommandContext::set_viewport_scissor(uint32_t width, uint32_t height)
    {
        // queue 種別を検証して RS state を設定する。
        if (type() != CommandListType::Graphics)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Viewport and scissor can only be set on graphics command lists.");
        }

        // ビューポートとシザー矩形の設定
        D3D12_VIEWPORT viewport{};
        viewport.TopLeftX = 0.0f;
        viewport.TopLeftY = 0.0f;
        viewport.Width = static_cast<float>(width);
        viewport.Height = static_cast<float>(height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        D3D12_RECT scissorRect{};
        scissorRect.left = 0;
        scissorRect.top = 0;
        scissorRect.right = static_cast<LONG>(width);
        scissorRect.bottom = static_cast<LONG>(height);

        m_commandList->RSSetViewports(1, &viewport);
        m_commandList->RSSetScissorRects(1, &scissorRect);
        
        return Result::ok();
    }
    Result DX12GpuCommandContext::set_primitive_topology(PrimitiveTopologyType topology)
    {
        // queue 種別を検証して IA state を設定する。
        if (type() != CommandListType::Graphics)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Primitive topology can only be set on graphics command lists.");
        }

        D3D12_PRIMITIVE_TOPOLOGY d3dTopology = convert_primitive_topology(topology);

        m_commandList->IASetPrimitiveTopology(d3dTopology);

        return Result::ok();
    }
    Result DX12GpuCommandContext::set_render_targets(const ViewHandle* renderTargetViews, uint32_t renderTargetCount, ViewHandle depthStencilView)
    {
        // queue 種別を検証して OM state を設定する。
        if (type() != CommandListType::Graphics)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Render targets can only be set on graphics command lists.");
        }

        // RTV ハンドルを CPU デスクリプタハンドルに変換する。
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvHandles;
        rtvHandles.reserve(renderTargetCount);
        rtvHandles.resize(renderTargetCount);
        for (uint32_t i = 0; i < renderTargetCount; ++i)
        {
            DX12ViewRecord* rtvRecord = nullptr;
            if (!m_viewManager.try_get_record(renderTargetViews[i], &rtvRecord))
            {
                return Result::fail(
                    Code::NotFound,
                    Severity::Error,
                    "View record was not found for the given render target handle.");
            }
            if (rtvRecord->desc.type != ViewType::RenderTarget)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "The view type of the given render target handle is not RenderTarget.");
            }
            rtvHandles[i] = m_descriptorAllocator.get_cpu_handle(rtvRecord->defaultTableIds[m_frameIndex]);
        }

        // DSV ハンドルを CPU デスクリプタハンドルに変換する。
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle{};
        if (depthStencilView.valid())
        {
            DX12ViewRecord* dsvRecord = nullptr;
            if (!m_viewManager.try_get_record(depthStencilView, &dsvRecord))
            {
                return Result::fail(
                    Code::NotFound,
                    Severity::Error,
                    "View record was not found for the given depth stencil handle.");
            }
            if (dsvRecord->desc.type != ViewType::DepthStencil)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "The view type of the given depth stencil handle is not DepthStencil.");
            }
            dsvHandle = m_descriptorAllocator.get_cpu_handle(dsvRecord->defaultTableIds[m_frameIndex]);
        }

        // レンダーターゲットとデプスステンシルをセットする。
        m_commandList->OMSetRenderTargets(
            renderTargetCount,
            rtvHandles.data(),
            false,
            depthStencilView.valid() ? &dsvHandle : nullptr);

        return Result::ok();
    }
    Result DX12GpuCommandContext::draw_instanced(uint32_t vertexCountPerInstance, uint32_t instanceCount, uint32_t startVertexLocation, uint32_t startInstanceLocation)
    {
        // draw 呼び出しは graphics queue でのみ有効とし、compute/copy queue での誤発行を防ぐ。
        if (type() != CommandListType::Graphics)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Draw can only be issued on a graphics command context.");
        }

        // 頂点/インスタンス範囲は呼び出し側の宣言どおりにそのまま発行し、pass 実装が draw パターンを選べるようにする。
        m_commandList->DrawInstanced(vertexCountPerInstance, instanceCount, startVertexLocation, startInstanceLocation);
        
        return Result::ok();
    }
    Result DX12GpuCommandContext::draw_indexed_instanced(uint32_t indexCountPerInstance, uint32_t instanceCount, uint32_t startIndexLocation, int32_t baseVertexLocation, uint32_t startInstanceLocation)
    {
        // indexed draw は graphics queue 以外で意味を持たない。
        if (type() != CommandListType::Graphics)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Indexed draw can only be issued on a graphics command context.");
        }

        // index 範囲と base vertex は呼び出し側の宣言どおりにそのまま流し、mesh slice 単位の描画を許可する。
        m_commandList->DrawIndexedInstanced(indexCountPerInstance, instanceCount, startIndexLocation, baseVertexLocation, startInstanceLocation);
        
        return Result::ok();
    }
    Result DX12GpuCommandContext::create_command_allocator(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type)
    {
        // コマンドアロケータの作成
        HRESULT hr = device.CreateCommandAllocator(
            type,
            IID_PPV_ARGS(&m_commandAllocator));
        if (FAILED(hr))
        {
            return Result::fail(
                Code::CreateFailed,
                Severity::Error,
                "Failed to create CommandAllocator.");
        }
        set_d3d12_name(m_commandAllocator.Get(), L"CommandContext CommandAllocator");

        return Result::ok();
    }
    Result DX12GpuCommandContext::create_command_list(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type)
    {
        // コマンドリストの作成
        HRESULT hr = device.CreateCommandList(
            0,
            type,
            m_commandAllocator.Get(),
            nullptr,
            IID_PPV_ARGS(&m_commandList));
        if (FAILED(hr))
        {
            return Result::fail(
                Code::CreateFailed,
                Severity::Error,
                "Failed to create CommandList.");
        }

        // オブジェクトに名前を付ける
        set_d3d12_name(m_commandList.Get(), L"CommandContext CommandList");

        // コマンドリストは生成直後にオープン状態になるのでクローズしておく
        m_commandList->Close();

        return Result::ok();
    }
    DX12GpuCommandQueue::DX12GpuCommandQueue(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type)
    {
        create_fence(device);
        create_fence_event();
        create_command_queue(device, type);
    }
    CommandListType DX12GpuCommandQueue::type() const
    {
        return m_type;
    }
    Result DX12GpuCommandQueue::submit(std::vector<ICommandContext*>& contexts)
    {
        std::vector<ID3D12CommandList*> commandLists;
        for (ICommandContext* context : contexts)
        {
            if (context == nullptr)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Command context is null.");
            }
            if (context->type() != m_type)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Command context type does not match the queue type.");
            }

            DX12GpuCommandContext& dx12Cmd = static_cast<DX12GpuCommandContext&>(*context);
            ID3D12CommandList* commandList = dx12Cmd.d3d12_command_list();
            if (commandList == nullptr)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Command list is null.");
            }
            commandLists.push_back(commandList);
        }

        m_commandQueue->ExecuteCommandLists(1, commandLists.data());
        return Result::ok();
    }
    Result DX12GpuCommandQueue::signal()
    {
        // submit 済み作業の完了点を外へ渡せるよう、フェンス値を進めて返す。
        if (!m_commandQueue || !m_fence)
        {
            return Result::fail(
                Code::InternalError,
                Severity::Fatal,
                "CommandQueue or Fence is not initialized.");
        }

        const UINT64 fence = ++m_fenceValue;
        const HRESULT hr = m_commandQueue->Signal(m_fence.Get(), fence);
        if (FAILED(hr))
        {
            return Result::fail(
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                "Failed to signal CommandQueue.");
        }

        return Result::ok();
    }
    Result DX12GpuCommandQueue::wait()
    {
        // 自前 fence の完了だけを監視し、再利用前の CPU 同期待機に使う。
        if (!m_fence || !m_fenceEvent)
        {
            return Result::fail(
                Code::InternalError,
                Severity::Fatal,
                "Fence or Fence event is not initialized.");
        }
        if (m_fence->GetCompletedValue() < m_fenceValue)
        {
            // 完了通知イベントを張り、指定値まで到達するまで待機する。
            m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
            WaitForSingleObject(m_fenceEvent, INFINITE);
        }
        return Result::ok();
    }
    Result DX12GpuCommandQueue::wait_for_queue(IQueueContext& queue)
    {
        DX12GpuCommandQueue& dx12Queue = static_cast<DX12GpuCommandQueue&>(queue);
        if (!m_fence || !dx12Queue.m_fence)
        {
            return Result::fail(
                Code::InternalError,
                Severity::Fatal,
                "Fence is not initialized.");
        }
        const HRESULT hr = m_commandQueue->Wait(dx12Queue.m_fence.Get(), dx12Queue.m_fenceValue);
        if (FAILED(hr))
        {
            return Result::fail(
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                "Failed to wait for another queue.");
        }

        return Result::ok();
    }
    Result DX12GpuCommandQueue::create_fence(ID3D12Device& device)
    {
        // フェンスの作成
        m_fence.Reset();
        m_fenceValue = 0;// 初期値0
        HRESULT hr = device.CreateFence(
            m_fenceValue,
            D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(&m_fence));
        if (FAILED(hr))
        {
            return Result::fail(
                Code::CreateFailed,
                Severity::Error,
                "Failed to create Fence.");
        }
        set_d3d12_name(m_fence.Get(), L"QueueContext Fence");
        return Result::ok();
    }
    Result DX12GpuCommandQueue::create_fence_event()
    {
        // イベントハンドルの作成
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (m_fenceEvent == nullptr)
        {
            return Result::fail(
                Code::CreateFailed,
                Severity::Error,
                "Failed to create Fence event.");
        }
        return Result::ok();
    }
    Result DX12GpuCommandQueue::create_command_queue(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type)
    {
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = type;
        HRESULT hr = device.CreateCommandQueue(
            &queueDesc,
            IID_PPV_ARGS(&m_commandQueue));
        if (FAILED(hr))
        {
            return Result::fail(
                Code::CreateFailed,
                Severity::Error,
                "Failed to create CommandQueue.");
        }
        set_d3d12_name(m_commandQueue.Get(), L"QueueContext CommandQueue");
        return Result::ok();
    }
    Result DX12CommandPool::get_command_context(CommandListType type, CommandContextLease& outContext)
    {
        switch (type)
        {
        case Cue::RHI::CommandListType::Graphics:
        {
            std::lock_guard lock(m_graphicsPoolMutex);
            // グラフィックスコマンドコンテキストをプールから取得
            auto context = m_graphicsContextPool.acquire();
            outContext = CommandContextLease(
                context.release(),
                [](ICommandContext* raw) {delete raw; });
        }
            break;
        case Cue::RHI::CommandListType::Compute:
        {
            std::lock_guard lock(m_computePoolMutex);
            // コンピュートコマンドコンテキストをプールから取得
                auto context = m_computeContextPool.acquire();
                outContext = CommandContextLease(
                    context.release(),
                    [](ICommandContext* raw) {delete raw; });
        }
            break;
        case Cue::RHI::CommandListType::Copy:
        {
            std::lock_guard lock(m_copyPoolMutex);
            // コピーコマンドコンテキストをプールから取得
                auto context = m_copyContextPool.acquire();
                outContext = CommandContextLease(
                    context.release(),
                    [](ICommandContext* raw) {delete raw; });
        }
            break;
        default:
            CUE_ASSERT_MSG(false, "Invalid command list type.");
            break;
        }
        return Result::ok();
    }
    Result DX12CommandPool::return_command_context(CommandContextLease& context)
    {
        CommandListType type = context->type();
        switch (type)
        {
        case Cue::RHI::CommandListType::Graphics:
        {
            // グラフィックスコマンドコンテキストをプールへ返却
            std::lock_guard lock(m_graphicsPoolMutex);
            m_graphicsContextPool.recycle(static_cast<DX12GpuCommandContext*>(context.release()));
        }
            break;
        case Cue::RHI::CommandListType::Compute:
        {
            // コンピュートコマンドコンテキストをプールへ返却
            std::lock_guard lock(m_computePoolMutex);
            m_computeContextPool.recycle(static_cast<DX12GpuCommandContext*>(context.release()));
        }
            break;
        case Cue::RHI::CommandListType::Copy:
        {
            // コピーコマンドコンテキストをプールへ返却
            std::lock_guard lock(m_copyPoolMutex);
            m_copyContextPool.recycle(static_cast<DX12GpuCommandContext*>(context.release()));
        }
            break;
        default:
            CUE_ASSERT_MSG(false, "Invalid command list type.");
            break;
        }
        return Result::ok();
    }
    Result DX12QueuePool::get_queue_context(CommandListType type, QueueContextLease& outContext)
    {
        switch (type)
        {
        case Cue::RHI::CommandListType::Graphics:
        {
            std::lock_guard lock(m_graphicsPoolMutex);
            // グラフィックスコマンドキューコンテキストをプールから取得
            auto context = m_graphicsQueuePool.acquire();
            outContext = QueueContextLease(
                context.release(),
                [](IQueueContext* raw) {delete raw; });
        }
            break;
        case Cue::RHI::CommandListType::Compute:
        {
            std::lock_guard lock(m_computePoolMutex);
            // コンピュートコマンドキューコンテキストをプールから取得
                auto context = m_computeQueuePool.acquire();
                outContext = QueueContextLease(
                    context.release(),
                    [](IQueueContext* raw) {delete raw; });
        }
            break;
        case Cue::RHI::CommandListType::Copy:
        {
            std::lock_guard lock(m_copyPoolMutex);
            // コピーコマンドキューコンテキストをプールから取得
                auto context = m_copyQueuePool.acquire();
                outContext = QueueContextLease(
                    context.release(),
                    [](IQueueContext* raw) {delete raw; });
        }
            break;
        default:
            break;
        }
        return Result::ok();
    }
    Result DX12QueuePool::return_queue_context(QueueContextLease& context)
    {
        CommandListType type = context->type();
        switch (type)
        {
        case Cue::RHI::CommandListType::Graphics:
        {
            // グラフィックスコマンドキューコンテキストをプールへ返却
            std::lock_guard lock(m_graphicsPoolMutex);
            m_graphicsQueuePool.recycle(static_cast<DX12GpuCommandQueue*>(context.release()));
        }
            break;
        case Cue::RHI::CommandListType::Compute:
        {
            // コンピュートコマンドキューコンテキストをプールへ返却
            std::lock_guard lock(m_computePoolMutex);
            m_computeQueuePool.recycle(static_cast<DX12GpuCommandQueue*>(context.release()));
        }
            break;
        case Cue::RHI::CommandListType::Copy:
        {
            // コピーコマンドキューコンテキストをプールへ返却
            std::lock_guard lock(m_copyPoolMutex);
            m_copyQueuePool.recycle(static_cast<DX12GpuCommandQueue*>(context.release()));
        }
            break;
        default:
            break;
        }
        return Result::ok();
    }
    Result DX12QueuePool::wait_for_graphics_queue()
    {
        std::lock_guard lock(m_graphicsPoolMutex);
        // グラフィックスコマンドキューコンテキストをプールから取得
        auto context = m_graphicsQueuePool.acquire();
        context->signal();
        context->wait();
        m_graphicsQueuePool.recycle(context.release());
        return Result::ok();
    }
}
