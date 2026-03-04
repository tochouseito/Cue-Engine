#include "DX12GpuCommand.h"
#include "DX12BufferManager.h"
#include "DX12TextureManager.h"

namespace Cue::GraphicsCore::DX12
{
    namespace
    {
        [[nodiscard]] D3D12_RESOURCE_STATES to_d3d12_resource_state(ResourceState state)
        {
            switch (state)
            {
            case ResourceState::Common:
                return D3D12_RESOURCE_STATE_COMMON;
            case ResourceState::CopySource:
                return D3D12_RESOURCE_STATE_COPY_SOURCE;
            case ResourceState::CopyDest:
                return D3D12_RESOURCE_STATE_COPY_DEST;
            case ResourceState::RenderTarget:
                return D3D12_RESOURCE_STATE_RENDER_TARGET;
            case ResourceState::UnorderedAccess:
                return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            case ResourceState::ShaderResource:
                return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            case ResourceState::DepthWrite:
                return D3D12_RESOURCE_STATE_DEPTH_WRITE;
            case ResourceState::Present:
                return D3D12_RESOURCE_STATE_PRESENT;
            default:
                return D3D12_RESOURCE_STATE_COMMON;
            }
        }
    }

    DX12CommandContext::DX12CommandContext(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type)
    {
        // 1) コマンドアロケータの作成
        create_command_allocator(device, type);

        // 2) コマンドリストの作成
        create_command_list(device, type);
    }
    DX12CommandContext::~DX12CommandContext()
    {
        // 1) 一時 RTV は command context 単位で確保しているため、破棄時に返却する。
        if (m_descriptorAllocator != nullptr && m_transientRtv.valid())
        {
            m_descriptorAllocator->free_table(m_transientRtv);
            m_transientRtv = {};
        }
    }
    Result DX12CommandContext::setup()
    {
        if(!m_descriptorAllocator)
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                0,
                "DescriptorAllocator is not bound to command context.");
        }
        auto srvHeap = m_descriptorAllocator->get_descriptor_heap(HeapType::CBV_SRV_UAV);
        m_commandList->SetDescriptorHeaps(1, &srvHeap);
        return Result::ok();
    }
    Result DX12CommandContext::reset()
    {
        if (!m_commandAllocator || !m_commandList)
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                0,
                "CommandAllocator or CommandList is null.");
        }

        // 1) コマンドアロケータのリセット
        HRESULT hr = m_commandAllocator->Reset();
        if (FAILED(hr))
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to reset CommandAllocator.");
        }

        // 2) コマンドリストのリセット
        hr = m_commandList->Reset(
            m_commandAllocator.Get(),
            nullptr);
        if (FAILED(hr))
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to reset CommandList.");
        }

        m_listEmpty = true;

        return Result::ok();
    }
    void DX12CommandContext::bind_resources(DX12BufferManager& bufferManager, DX12TextureManager& textureManager, IViewManager& viewManager, DescriptorAllocator& descriptorAllocator) noexcept
    {
        // 1) manager 参照は acquire 時に再設定し、pool を跨いでも最新構成へ合わせる。
        m_bufferManager = &bufferManager;
        m_textureManager = &textureManager;
        m_viewManager = &viewManager;
        m_descriptorAllocator = &descriptorAllocator;
    }
    Result DX12CommandContext::close()
    {
        if (!m_commandList)
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                0,
                "CommandList is null.");
        }

        // コマンドリストのクローズ
        HRESULT hr = m_commandList->Close();
        if (FAILED(hr))
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to close CommandList.");
        }

        return Result::ok();
    }
    Result DX12CommandContext::create_command_allocator(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type)
    {
        // コマンドアロケータの作成
        HRESULT hr = device.CreateCommandAllocator(
            type,
            IID_PPV_ARGS(&m_commandAllocator));
        if (FAILED(hr))
        {
            return Result::fail(
                Facility::Graphics,
                Code::CreationFailed,
                Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to create CommandAllocator.");
        }
        SetD3D12Name(m_commandAllocator.Get(), L"CommandContext CommandAllocator");
        return Result::ok();
    }
    Result DX12CommandContext::create_command_list(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type)
    {
        // 1) コマンドリストの作成
        HRESULT hr = device.CreateCommandList(
            0,
            type,
            m_commandAllocator.Get(),
            nullptr,
            IID_PPV_ARGS(&m_commandList));
        if (FAILED(hr))
        {
            return Result::fail(
                Facility::Graphics,
                Code::CreationFailed,
                Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to create CommandList.");
        }

        // 2) オブジェクトに名前を付ける
        SetD3D12Name(m_commandList.Get(), L"CommandContext CommandList");

        // 3) コマンドリストは生成直後にオープン状態になるのでクローズしておく
        m_commandList->Close();

        return Result::ok();
    }
    Result DX12CommandContext::resolve_buffer_resource(BufferHandle handle, GpuBufferResource*& outBuffer) const
    {
        // 1) バリア対象の buffer 実体は manager にのみあるので、command context から直接保持しない。
        if (m_bufferManager == nullptr)
        {
            return Result::fail(Facility::Graphics, Code::InvalidState, Severity::Error, 0, "Buffer manager is not bound to command context");
        }
        return m_bufferManager->try_get_buffer(handle, outBuffer);
    }
    Result DX12CommandContext::resolve_texture_resource(TextureHandle handle, GpuTextureResource*& outTexture) const
    {
        // 1) テクスチャ解決も manager 経由に限定し、外部所有の back buffer を正しく逆引きする。
        if (m_textureManager == nullptr)
        {
            return Result::fail(Facility::Graphics, Code::InvalidState, Severity::Error, 0, "Texture manager is not bound to command context");
        }
        return m_textureManager->try_get_texture(handle, outTexture);
    }
    Result DX12CommandContext::resource_barrier(const ResourceBarrierDesc& barrier)
    {
        // 1) 単発バリアも複数版へ集約し、実装差分を増やさない。
        return resource_barriers(&barrier, 1);
    }
    Result DX12CommandContext::resource_barriers(const ResourceBarrierDesc* barriers, size_t count)
    {
        // 1) 空入力はそのまま成功扱いにして、呼び出し側の分岐を増やさない。
        if (barriers == nullptr || count == 0)
        {
            return Result::ok();
        }
        if (m_commandList == nullptr)
        {
            return Result::fail(Facility::Graphics, Code::InvalidState, Severity::Error, 0, "Command list is null.");
        }

        // 2) GraphicsCore の抽象バリアを D3D12 transition barrier へ変換する。
        std::vector<D3D12_RESOURCE_BARRIER> nativeBarriers;
        nativeBarriers.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            const ResourceBarrierDesc& barrier = barriers[i];
            ID3D12Resource* resource = nullptr;
            if (barrier.kind == ResourceKind::Buffer)
            {
                GpuBufferResource* buffer = nullptr;
                const Result resolveResult = resolve_buffer_resource(BufferHandle{ barrier.index, barrier.generation }, buffer);
                if (!resolveResult)
                {
                    return resolveResult;
                }
                resource = buffer->get_resource();
            }
            else
            {
                GpuTextureResource* texture = nullptr;
                const Result resolveResult = resolve_texture_resource(TextureHandle{ barrier.index, barrier.generation }, texture);
                if (!resolveResult)
                {
                    return resolveResult;
                }
                resource = texture->get_resource();
            }

            D3D12_RESOURCE_BARRIER nativeBarrier{};
            nativeBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            nativeBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            nativeBarrier.Transition.pResource = resource;
            nativeBarrier.Transition.StateBefore = to_d3d12_resource_state(barrier.before);
            nativeBarrier.Transition.StateAfter = to_d3d12_resource_state(barrier.after);
            nativeBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            nativeBarriers.push_back(nativeBarrier);
        }

        // 3) 実バリアをコマンドリストへ記録し、後続の clear/draw が正しい状態を見るようにする。
        m_commandList->ResourceBarrier(static_cast<UINT>(nativeBarriers.size()), nativeBarriers.data());
        m_listEmpty = false;
        return Result::ok();
    }
    Result DX12CommandContext::clear_render_target(TextureHandle handle, const float clearColor[4])
    {
        // 1) RTV を動的に引いて clear し、pass 側が swap chain 実体を知らなくて済むようにする。
        if (m_commandList == nullptr || m_descriptorAllocator == nullptr)
        {
            return Result::fail(Facility::Graphics, Code::InvalidState, Severity::Error, 0, "Command context is not bound to RTV resources.");
        }

        GpuTextureResource* texture = nullptr;
        const Result resolveResult = resolve_texture_resource(handle, texture);
        if (!resolveResult)
        {
            return resolveResult;
        }
        if (!m_transientRtv.valid())
        {
            m_transientRtv = m_descriptorAllocator->allocate(DescriptorAllocator::TableKind::RenderTargets);
            if (!m_transientRtv.valid())
            {
                return Result::fail(Facility::Graphics, Code::CreationFailed, Severity::Error, 0, "Failed to allocate transient RTV table.");
            }
        }

        const Result createRtvResult = m_descriptorAllocator->create_rtv(m_transientRtv, texture, texture->get_resource_desc().Format);
        if (!createRtvResult)
        {
            return createRtvResult;
        }

        // 2) ClearRenderTargetView 自体は RTV ハンドルだけで実行できるため、最小コマンドで済ませる。
        const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_descriptorAllocator->get_cpu_handle(m_transientRtv);
        m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
        m_listEmpty = false;
        return Result::ok();
    }
    Result DX12CommandContext::set_viewport_scissor(uint32_t width, uint32_t height)
    {
        // 1) Graphics queue 以外では RS state を触れないため、呼び出し側の誤用をここで止める。
        if (type() != CommandListType::Graphics)
        {
            return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Warning, 0, "Viewport/scissor can only be set on a graphics command context.");
        }
        if (m_commandList == nullptr)
        {
            return Result::fail(Facility::Graphics, Code::InvalidState, Severity::Error, 0, "Command list is null.");
        }

        // 2) FrameGraph の画面サイズをそのまま rasterizer state に反映し、pass 側の毎回設定を減らす。
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
        m_listEmpty = false;
        return Result::ok();
    }
    Result DX12CommandContext::set_render_targets(const ViewHandle* renderTargetViews, uint32_t renderTargetCount, ViewHandle depthStencilView)
    {
        // 1) OM は graphics queue 専用なので、queue 種別と manager バインドの不整合を先に潰す。
        if (type() != CommandListType::Graphics)
        {
            return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Warning, 0, "Render targets can only be set on a graphics command context.");
        }
        if (m_commandList == nullptr || m_viewManager == nullptr)
        {
            return Result::fail(Facility::Graphics, Code::InvalidState, Severity::Error, 0, "Command context is not bound to view resources.");
        }
        if (renderTargetCount > D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT)
        {
            return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Error, 0, "Render target count exceeds D3D12 limit.");
        }
        if (renderTargetCount > 0 && renderTargetViews == nullptr)
        {
            return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Error, 0, "Render target view array is null.");
        }

        // 2) view manager が保持する CPU descriptor へ引き直し、FrameGraph 側は ViewHandle だけを渡せばよいようにする。
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> nativeRenderTargetHandles{};
        for (uint32_t i = 0; i < renderTargetCount; ++i)
        {
            if (!renderTargetViews[i].valid())
            {
                return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Error, 0, "Render target view handle is invalid.");
            }

            DescriptorHandle descriptorHandle{};
            const Result descriptorResult = m_viewManager->get_descriptor_handle(renderTargetViews[i], descriptorHandle);
            if (!descriptorResult)
            {
                return descriptorResult;
            }
            if (!descriptorHandle.valid() || descriptorHandle.shaderVisible)
            {
                return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Error, 0, "Render target descriptor is not CPU-only.");
            }

            nativeRenderTargetHandles[i].ptr = static_cast<SIZE_T>(descriptorHandle.cpuPtr);
        }

        D3D12_CPU_DESCRIPTOR_HANDLE nativeDepthStencilHandle{};
        D3D12_CPU_DESCRIPTOR_HANDLE* depthStencilHandlePtr = nullptr;
        if (depthStencilView.valid())
        {
            DescriptorHandle descriptorHandle{};
            const Result descriptorResult = m_viewManager->get_descriptor_handle(depthStencilView, descriptorHandle);
            if (!descriptorResult)
            {
                return descriptorResult;
            }
            if (!descriptorHandle.valid() || descriptorHandle.shaderVisible)
            {
                return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Error, 0, "Depth stencil descriptor is not CPU-only.");
            }

            nativeDepthStencilHandle.ptr = static_cast<SIZE_T>(descriptorHandle.cpuPtr);
            depthStencilHandlePtr = &nativeDepthStencilHandle;
        }

        // 3) graphics pass 実行前に OM へ反映して、ImGui など backend 直叩きの描画も同じ bind 経路に揃える。
        m_commandList->OMSetRenderTargets(
            renderTargetCount,
            renderTargetCount == 0 ? nullptr : nativeRenderTargetHandles.data(),
            FALSE,
            depthStencilHandlePtr);
        m_listEmpty = false;
        return Result::ok();
    }
    DX12CommandPool::DX12CommandPool(DX12RenderDevice& device)
        : m_graphicsContextPool(
            32,
            [](DX12GraphicsCommandContext& ctx)
            {
                (void)ctx;
            },
            [d3d12Device = device.get_d3d12_device()]()
            {
                return std::make_unique<DX12GraphicsCommandContext>(*d3d12Device);
            })
        , m_computeContextPool(
            32,
            [](DX12ComputeCommandContext& ctx)
            {
                (void)ctx;
            },
            [d3d12Device = device.get_d3d12_device()]()
            {
                return std::make_unique<DX12ComputeCommandContext>(*d3d12Device);
            })
        , m_copyContextPool(
            32,
            [](DX12CopyCommandContext& ctx)
            {
                (void)ctx;
            },
            [d3d12Device = device.get_d3d12_device()]()
            {
                return std::make_unique<DX12CopyCommandContext>(*d3d12Device);
            })
    {
    }
    Result DX12CommandPool::initialize()
    {
        m_graphicsContextPool.prewarm(1);
        m_computeContextPool.prewarm(1);
        m_copyContextPool.prewarm(1);
        return Result::ok();
    }
    void DX12CommandPool::bind_resources(DX12BufferManager& bufferManager, DX12TextureManager& textureManager, IViewManager& viewManager, DescriptorAllocator& descriptorAllocator) noexcept
    {
        // 1) command context が resource/barrier/clear を実行できるよう、backend 所有 manager を束ねて渡す。
        m_bufferManager = &bufferManager;
        m_textureManager = &textureManager;
        m_viewManager = &viewManager;
        m_descriptorAllocator = &descriptorAllocator;
    }
    Result DX12CommandPool::acquire_context(CommandListType type, CommandContextLease& outContext)
    {
        // 1) 次に使う queue 種別の完了済み command context を先に回収して、再利用可能プールへ戻す。
        collect_completed_contexts(type);

        // 2) 完了済みのものを戻した後で、必要な command context を取得する。
        switch (type)
        {
        case CommandListType::Graphics:
        {
            auto pooled = m_graphicsContextPool.acquire();
            if (m_bufferManager != nullptr && m_textureManager != nullptr && m_viewManager != nullptr && m_descriptorAllocator != nullptr)
            {
                pooled->bind_resources(*m_bufferManager, *m_textureManager, *m_viewManager, *m_descriptorAllocator);
            }
            outContext = CommandContextLease(
                pooled.release(),
                [this](ICommandContext* raw) { m_graphicsContextPool.recycle(static_cast<DX12GraphicsCommandContext*>(raw)); });
            return Result::ok();
        }
        case CommandListType::Compute:
        {
            auto pooled = m_computeContextPool.acquire();
            if (m_bufferManager != nullptr && m_textureManager != nullptr && m_viewManager != nullptr && m_descriptorAllocator != nullptr)
            {
                pooled->bind_resources(*m_bufferManager, *m_textureManager, *m_viewManager, *m_descriptorAllocator);
            }
            outContext = CommandContextLease(
                pooled.release(),
                [this](ICommandContext* raw) { m_computeContextPool.recycle(static_cast<DX12ComputeCommandContext*>(raw)); });
            return Result::ok();
        }
        case CommandListType::Copy:
        {
            auto pooled = m_copyContextPool.acquire();
            if (m_bufferManager != nullptr && m_textureManager != nullptr && m_viewManager != nullptr && m_descriptorAllocator != nullptr)
            {
                pooled->bind_resources(*m_bufferManager, *m_textureManager, *m_viewManager, *m_descriptorAllocator);
            }
            outContext = CommandContextLease(
                pooled.release(),
                [this](ICommandContext* raw) { m_copyContextPool.recycle(static_cast<DX12CopyCommandContext*>(raw)); });
            return Result::ok();
        }
        default:
            return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Warning, 0, "Unsupported command list type");
        }
    }
    Result DX12CommandPool::retire_context(CommandContextLease&& context, IQueueContext& queueContext, const QueueSyncPoint& completionPoint)
    {
        // 1) FrameGraph から渡された active context と queue を DX12 実装として検証する。
        auto* dx12Context = dynamic_cast<DX12CommandContext*>(context.get());
        auto* dx12QueueContext = dynamic_cast<DX12QueueContext*>(&queueContext);
        if (dx12Context == nullptr || dx12QueueContext == nullptr)
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidArg,
                Severity::Error,
                0,
                "Retired command context or queue context is not a valid DX12 object.");
        }

        // 2) 完了 fence にひも付く in-flight リストへ移し、GPU 完了まで pool へ返却しない。
        InFlightCommandContext inFlight{};
        inFlight.context = std::unique_ptr<DX12CommandContext>(static_cast<DX12CommandContext*>(context.release()));
        inFlight.queueContext = dx12QueueContext;
        inFlight.completionPoint = completionPoint;
        m_inFlightContexts.push_back(std::move(inFlight));
        return Result::ok();
    }
    void DX12CommandPool::collect_completed_contexts(CommandListType type)
    {
        // 1) 要求された queue 種別に対して、完了 fence に到達した command context だけを回収する。
        for (auto it = m_inFlightContexts.begin(); it != m_inFlightContexts.end();)
        {
            if (!it->context || it->context->type() != type)
            {
                ++it;
                continue;
            }
            if (it->queueContext == nullptr || !it->queueContext->is_complete(it->completionPoint))
            {
                ++it;
                continue;
            }

            // 2) GPU が使い終わったものだけ pool へ返し、recycle 時の reset を安全にする。
            recycle_context(std::move(it->context));
            it = m_inFlightContexts.erase(it);
        }
    }
    void DX12CommandPool::recycle_context(std::unique_ptr<DX12CommandContext> context)
    {
        // 1) 実体の queue 種別ごとに元の pool へ戻し、既存の reset 動作を再利用する。
        if (!context)
        {
            return;
        }

        switch (context->type())
        {
        case CommandListType::Graphics:
            m_graphicsContextPool.recycle(static_cast<DX12GraphicsCommandContext*>(context.release()));
            return;
        case CommandListType::Compute:
            m_computeContextPool.recycle(static_cast<DX12ComputeCommandContext*>(context.release()));
            return;
        case CommandListType::Copy:
            m_copyContextPool.recycle(static_cast<DX12CopyCommandContext*>(context.release()));
            return;
        default:
            return;
        }
    }
    DX12QueueContext::DX12QueueContext(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type)
    {
        create_fence(device);
        create_fence_event();
        create_command_queue(device, type);
    }
    DX12QueueContext::~DX12QueueContext()
    {
        if (m_commandQueue && m_fence)
        {
            wait_for_last_signal();
        }
        if (m_fenceEvent)
        {
            CloseHandle(m_fenceEvent);
            m_fenceEvent = nullptr;
        }
        m_fence.Reset();
        m_commandQueue.Reset();
    }
    Result DX12QueueContext::submit(ICommandContext& cmd)
    {
        if (cmd.type() != type() || !m_commandQueue)
        {
            return Result::fail(
                Facility::GraphicsCore,
                Code::InvalidArg,
                Severity::Error,
                0,
                "Command list type does not match queue type or command queue is not initialized.");
        }

        DX12CommandContext& dx12Cmd = static_cast<DX12CommandContext&>(cmd);
        ID3D12GraphicsCommandList* commandList = dx12Cmd.get_command_list();
        if (commandList == nullptr)
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                0,
                "Command list is null.");
        }

        ID3D12CommandList* lists[] = { commandList };
        m_commandQueue->ExecuteCommandLists(1, lists);
        return Result::ok();
    }
    Result DX12QueueContext::signal(QueueSyncPoint& outPoint)
    {
        // 1) submit 済み作業の完了点を外へ渡せるよう、フェンス値を進めて返す。
        if (!m_commandQueue || !m_fence)
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                0,
                "Queue or fence is not initialized.");
        }

        const UINT64 fence = ++m_fenceValue;
        const HRESULT hr = m_commandQueue->Signal(m_fence.Get(), fence);
        if (FAILED(hr))
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to signal queue fence.");
        }

        outPoint.queueType = type();
        outPoint.value = fence;
        m_lastSignalPoint = outPoint;
        return Result::ok();
    }
    Result DX12QueueContext::wait(const IQueueContext& producerQueue, const QueueSyncPoint& point)
    {
        // 1) 依存元 queue の fence を consumer queue に積み、GPU 上で待機させる。
        if (!m_commandQueue)
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                0,
                "Command queue is not initialized.");
        }

        // 2) 共通 interface から渡された producer queue を DX12 実装へ落とし込む。
        const auto* dx12ProducerQueue = dynamic_cast<const DX12QueueContext*>(&producerQueue);
        if (dx12ProducerQueue == nullptr || dx12ProducerQueue->m_fence == nullptr)
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidArg,
                Severity::Error,
                0,
                "Producer queue is not a valid DX12 queue context.");
        }

        // 3) CPU を止めず queue wait を積み、クロスキュー依存を GPU 同期で解決する。
        const HRESULT hr = m_commandQueue->Wait(dx12ProducerQueue->m_fence.Get(), point.value);
        if (FAILED(hr))
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to enqueue queue wait.");
        }

        return Result::ok();
    }
    bool DX12QueueContext::is_complete(const QueueSyncPoint& point) const
    {
        // 1) 完了確認は signal した fence 値との大小比較だけで行い、CPU wait は発生させない。
        if (!m_fence)
        {
            return false;
        }

        return m_fence->GetCompletedValue() >= point.value;
    }
    Result DX12QueueContext::wait_for_last_signal()
    {
        // 1) recycle / shutdown では自 queue の最後の signal 完了を CPU で待つ。
        if (m_lastSignalPoint.value != 0)
        {
            return wait_for_fence_value(m_lastSignalPoint.value);
        }
        return Result::ok();
    }
    Result DX12QueueContext::wait_for_fence_value(uint64_t value)
    {
        // 1) 自前 fence の完了だけを監視し、再利用前の CPU 同期待機に使う。
        if (!m_fence || !m_fenceEvent)
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                0,
                "Fence or fence event is not initialized.");
        }
        if (m_fence->GetCompletedValue() < value)
        {
            // 2) 完了通知イベントを張り、指定値まで到達するまで待機する。
            m_fence->SetEventOnCompletion(value, m_fenceEvent);
            WaitForSingleObject(m_fenceEvent, INFINITE);
        }
        return Result::ok();
    }
    Result DX12QueueContext::create_fence(ID3D12Device& device)
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
                Facility::Graphics,
                Code::CreationFailed,
                Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to create Fence.");
        }
        SetD3D12Name(m_fence.Get(), L"QueueContext Fence");
        return Result::ok();
    }
    Result DX12QueueContext::create_fence_event()
    {
        // イベントハンドルの作成
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (m_fenceEvent == nullptr)
        {
            return Result::fail(
                Facility::Graphics,
                Code::CreationFailed,
                Severity::Error,
                GetLastError(),
                "Failed to create Fence event handle.");
        }
        return Result::ok();
    }
    Result DX12QueueContext::create_command_queue(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type)
    {
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = type;
        HRESULT hr = device.CreateCommandQueue(
            &queueDesc,
            IID_PPV_ARGS(&m_commandQueue));
        if (FAILED(hr))
        {
            return Result::fail(
                Facility::Graphics,
                Code::CreationFailed,
                Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to create CommandQueue.");
        }
        SetD3D12Name(m_commandQueue.Get(), L"QueueContext CommandQueue");
        return Result::ok();
    }
    DX12QueuePool::DX12QueuePool(DX12RenderDevice& device)
        : m_graphicsQueuePool(
            4,
            [](DX12GraphicsQueueContext& ctx)
            {
                ctx.wait_for_last_signal();
            },
            [d3d12Device = device.get_d3d12_device()]()
            {
                return std::make_unique<DX12GraphicsQueueContext>(*d3d12Device);
            })
        , m_computeQueuePool(
            4,
            [](DX12ComputeQueueContext& ctx)
            {
                ctx.wait_for_last_signal();
            },
            [d3d12Device = device.get_d3d12_device()]()
            {
                return std::make_unique<DX12ComputeQueueContext>(*d3d12Device);
            })
        , m_copyQueuePool(
            4,
            [](DX12CopyQueueContext& ctx)
            {
                ctx.wait_for_last_signal();
            },
            [d3d12Device = device.get_d3d12_device()]()
            {
                return std::make_unique<DX12CopyQueueContext>(*d3d12Device);
            })
    {
    }
    Result DX12QueuePool::initialize()
    {
        m_graphicsQueuePool.prewarm(k_graphicsQueueCount);
        m_computeQueuePool.prewarm(k_computeQueueCount);
        m_copyQueuePool.prewarm(k_copyQueueCount);
        return Result::ok();
    }
    Result DX12QueuePool::acquire_queue(CommandListType type, QueueContextLease& outQueue)
    {
        switch (type)
        {
        case CommandListType::Graphics:
        {
            auto pooled = m_graphicsQueuePool.acquire();
            outQueue = QueueContextLease(
                pooled.release(),
                [this](IQueueContext* raw) { m_graphicsQueuePool.recycle(static_cast<DX12GraphicsQueueContext*>(raw)); });
            return Result::ok();
        }
        case CommandListType::Compute:
        {
            auto pooled = m_computeQueuePool.acquire();
            outQueue = QueueContextLease(
                pooled.release(),
                [this](IQueueContext* raw) { m_computeQueuePool.recycle(static_cast<DX12ComputeQueueContext*>(raw)); });
            return Result::ok();
        }
        case CommandListType::Copy:
        {
            auto pooled = m_copyQueuePool.acquire();
            outQueue = QueueContextLease(
                pooled.release(),
                [this](IQueueContext* raw) { m_copyQueuePool.recycle(static_cast<DX12CopyQueueContext*>(raw)); });
            return Result::ok();
        }
        default:
            return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Warning, 0, "Unsupported queue type");
        }
    }
} // namespace Cue::GraphicsCore::DX12
