#include "DX12GpuCommand.h"

namespace Cue::RHI::DX12
{
    namespace
    {
        constexpr UINT k_eventMetadataAnsi = 1u;

        [[nodiscard]] Result validate_root_binding_command_type(CommandListType type, const char* bindTarget)
        {
            if (type == CommandListType::Copy)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    bindTarget);
            }

            return Result::ok();
        }

        [[nodiscard]] D3D12_RESOURCE_STATES normalize_resource_state_for_command_list(
            CommandListType commandListType,
            D3D12_RESOURCE_STATES state) noexcept
        {
            if (commandListType == CommandListType::Copy)
            {
                if (state != D3D12_RESOURCE_STATE_COMMON &&
                    state != D3D12_RESOURCE_STATE_COPY_SOURCE &&
                    state != D3D12_RESOURCE_STATE_COPY_DEST)
                {
                    return D3D12_RESOURCE_STATE_COMMON;
                }
            }

            if (commandListType == CommandListType::Compute)
            {
                if ((state & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) != 0)
                {
                    state &= ~D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                    state |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                }
            }

            return state;
        }
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
        // DrawIndexedInstanced 用の command signature を用意する。
        create_draw_indexed_command_signature(device);
        // timestamp query の受け皿を作る。
        create_timestamp_resources(device, type);

        m_type = convert_command_list_type(type);
    }
    Result DX12GpuCommandContext::setup(uint32_t frameIndex, uint32_t bufferCount)
    {
        // copy command list は descriptor heap を扱えないため、setup は no-op で返す。
        if (type() == CommandListType::Copy)
        {
            m_frameIndex = frameIndex;
            m_bufferCount = bufferCount;
            return Result::ok();
        }

        m_frameIndex = frameIndex;
        m_bufferCount = bufferCount;
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
    bool DX12GpuCommandContext::supports_timestamps() const
    {
        return m_timestampQueryHeap != nullptr &&
            m_timestampReadbackBuffer != nullptr &&
            m_timestampReadbackMappedData != nullptr &&
            type() != CommandListType::Copy;
    }
    Result DX12GpuCommandContext::write_timestamp(uint32_t queryIndex)
    {
        if (!supports_timestamps())
        {
            return Result::ok();
        }
        if (queryIndex >= k_maxTimestampQueryCount)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Timestamp query index is out of range.");
        }

        m_commandList->EndQuery(
            m_timestampQueryHeap.Get(),
            D3D12_QUERY_TYPE_TIMESTAMP,
            queryIndex);
        return Result::ok();
    }
    Result DX12GpuCommandContext::resolve_timestamps(
        uint32_t firstQueryIndex, uint32_t queryCount)
    {
        if (!supports_timestamps())
        {
            return Result::ok();
        }
        if (queryCount == 0)
        {
            return Result::ok();
        }
        if (firstQueryIndex + queryCount > k_maxTimestampQueryCount)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Timestamp query range is out of bounds.");
        }

        m_commandList->ResolveQueryData(
            m_timestampQueryHeap.Get(),
            D3D12_QUERY_TYPE_TIMESTAMP,
            firstQueryIndex,
            queryCount,
            m_timestampReadbackBuffer.Get(),
            static_cast<UINT64>(firstQueryIndex) * sizeof(uint64_t));
        return Result::ok();
    }
    Result DX12GpuCommandContext::read_timestamp(
        uint32_t queryIndex, uint64_t& outValue) const
    {
        outValue = 0;
        if (!supports_timestamps())
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "Timestamp queries are not supported on this command context.");
        }
        if (queryIndex >= k_maxTimestampQueryCount)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Timestamp query index is out of range.");
        }

        const uint64_t* values =
            reinterpret_cast<const uint64_t*>(m_timestampReadbackMappedData);
        outValue = values[queryIndex];
        return Result::ok();
    }
    void DX12GpuCommandContext::set_pending_fence(
        IQueueContext* a_queue,
        uint64_t a_fenceValue)
    {
        m_pendingQueue = a_queue;
        m_pendingFenceValue = a_fenceValue;
    }
    Result DX12GpuCommandContext::wait_for_pending_fence()
    {
        if (m_pendingQueue == nullptr || m_pendingFenceValue == 0)
        {
            return Result::ok();
        }

        Result result = m_pendingQueue->wait_for_fence(m_pendingFenceValue);
        if (!result)
        {
            return result;
        }

        m_pendingQueue = nullptr;
        m_pendingFenceValue = 0;
        return Result::ok();
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
        uint32_t resourceIndex = 0;
        Result result = resolve_slice_index(record->defaultResources.size(), resourceIndex);
        if (!result)
        {
            return Result::fail(
                result.code,
                Severity::Error,
                "Failed to resolve buffer slice index for the current frame.");
        }
        DX12GpuResource* resource = &record->defaultResources[resourceIndex];
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
        d3d12Barrier.Transition.StateBefore = normalize_resource_state_for_command_list(type(), resource->get_current_d3d12_state());
        d3d12Barrier.Transition.StateAfter = normalize_resource_state_for_command_list(type(), convert_resource_state(desc.after));
        d3d12Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        if (d3d12Barrier.Transition.StateBefore == d3d12Barrier.Transition.StateAfter)
        {
            resource->set_current_state(d3d12Barrier.Transition.StateAfter);
            return Result::ok();
        }

        m_commandList->ResourceBarrier(1, &d3d12Barrier);
        resource->set_current_state(d3d12Barrier.Transition.StateAfter);

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
        uint32_t resourceIndex = 0;
        Result result = resolve_slice_index(record->defaultResources.size(), resourceIndex);
        if (!result)
        {
            return Result::fail(
                result.code,
                Severity::Error,
                "Failed to resolve texture slice index for the current frame.");
        }
        DX12GpuResource* resource = &record->defaultResources[resourceIndex];
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
        d3d12Barrier.Transition.StateBefore = normalize_resource_state_for_command_list(type(), resource->get_current_d3d12_state());
        d3d12Barrier.Transition.StateAfter = normalize_resource_state_for_command_list(type(), convert_resource_state(desc.after));
        d3d12Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        if (d3d12Barrier.Transition.StateBefore == d3d12Barrier.Transition.StateAfter)
        {
            resource->set_current_state(d3d12Barrier.Transition.StateAfter);
            return Result::ok();
        }

        m_commandList->ResourceBarrier(1, &d3d12Barrier);
        resource->set_current_state(d3d12Barrier.Transition.StateAfter);

        return Result::ok();
    }
    Result DX12GpuCommandContext::copy_buffer_region(const BufferCopyRegion& region)
    {
        if (region.byteSize == 0)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Copy buffer region byte size must be greater than 0.");
        }

        DX12GpuResource* srcResource = nullptr;
        Result result = resolve_upload_buffer(region.srcBufferHandle, region.srcUploadResourceIndex, &srcResource);
        if (!result)
        {
            return result;
        }

        DX12GpuResource* dstResource = nullptr;
        result = resolve_default_buffer(region.dstBufferHandle, region.dstDefaultResourceIndex, &dstResource);
        if (!result)
        {
            return result;
        }

        if (region.srcByteOffset + region.byteSize > srcResource->get_buffer_size())
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Copy source range exceeds the source buffer size.");
        }
        if (region.dstByteOffset + region.byteSize > dstResource->get_buffer_size())
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Copy destination range exceeds the destination buffer size.");
        }

        m_commandList->CopyBufferRegion(
            dstResource->get_resource(),
            region.dstByteOffset,
            srcResource->get_resource(),
            region.srcByteOffset,
            region.byteSize);

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
        uint32_t descriptorIndex = 0;
        Result result = resolve_slice_index(record->defaultTableIds.size(), descriptorIndex);
        if (!result)
        {
            return Result::fail(
                result.code,
                Severity::Error,
                "Failed to resolve render target descriptor index for the current frame.");
        }
        auto cpuHandle = m_descriptorAllocator.get_cpu_handle(record->defaultTableIds[descriptorIndex]);

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

        uint32_t descriptorIndex = 0;
        Result result = resolve_slice_index(record->defaultTableIds.size(), descriptorIndex);
        if (!result)
        {
            return Result::fail(
                result.code,
                Severity::Error,
                "Failed to resolve depth stencil descriptor index for the current frame.");
        }
        auto cpuHandle = m_descriptorAllocator.get_cpu_handle(record->defaultTableIds[descriptorIndex]);

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
    Result DX12GpuCommandContext::clear_unordered_access_uint(ViewHandle handle, const uint32_t clearValues[4])
    {
        Result result = validate_root_binding_command_type(
            type(),
            "UAV clear can only be issued on graphics or compute command contexts.");
        if (!result)
        {
            return result;
        }

        DX12ViewRecord* viewRecord = nullptr;
        if (!m_viewManager.try_get_record(handle, &viewRecord) || viewRecord == nullptr)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "View record was not found for the given UAV handle.");
        }
        if (viewRecord->desc.type != ViewType::UnorderedAccessBuffer &&
            viewRecord->desc.type != ViewType::UnorderedAccessRawBuffer)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "The view type of the given handle is not a UAV buffer.");
        }

        DX12BufferRecord* bufferRecord = nullptr;
        if (!m_bufferManager.try_get_record(viewRecord->desc.bufferHandle, &bufferRecord) || bufferRecord == nullptr)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Buffer record was not found for the given UAV view handle.");
        }

        if (!viewRecord->defaultTableIds.empty() && !bufferRecord->defaultResources.empty())
        {
            uint32_t descriptorIndex = 0;
            result = resolve_slice_index(viewRecord->defaultTableIds.size(), descriptorIndex);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to resolve default UAV descriptor index for the current frame.");
            }

            uint32_t resourceIndex = 0;
            result = resolve_slice_index(bufferRecord->defaultResources.size(), resourceIndex);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to resolve default UAV buffer slice index for the current frame.");
            }

            DX12GpuResource& resource = bufferRecord->defaultResources[resourceIndex];
            const D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = m_descriptorAllocator.get_gpu_handle(viewRecord->defaultTableIds[descriptorIndex]);
            const D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_descriptorAllocator.get_cpu_handle(viewRecord->defaultTableIds[descriptorIndex]);
            m_commandList->ClearUnorderedAccessViewUint(
                gpuHandle,
                cpuHandle,
                resource.get_resource(),
                const_cast<uint32_t*>(clearValues),
                0,
                nullptr);
            return Result::ok();
        }

        if (!viewRecord->uploadTableIds.empty() && !bufferRecord->uploadResources.empty())
        {
            uint32_t descriptorIndex = 0;
            result = resolve_slice_index(viewRecord->uploadTableIds.size(), descriptorIndex);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to resolve upload UAV descriptor index for the current frame.");
            }

            uint32_t resourceIndex = 0;
            result = resolve_slice_index(bufferRecord->uploadResources.size(), resourceIndex);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to resolve upload UAV buffer slice index for the current frame.");
            }

            DX12GpuResource& resource = bufferRecord->uploadResources[resourceIndex];
            const D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = m_descriptorAllocator.get_gpu_handle(viewRecord->uploadTableIds[descriptorIndex]);
            const D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_descriptorAllocator.get_cpu_handle(viewRecord->uploadTableIds[descriptorIndex]);
            m_commandList->ClearUnorderedAccessViewUint(
                gpuHandle,
                cpuHandle,
                resource.get_resource(),
                const_cast<uint32_t*>(clearValues),
                0,
                nullptr);
            return Result::ok();
        }

        return Result::fail(
            Code::NotFound,
            Severity::Error,
            "No clearable UAV resource was found for the given handle.");
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
    Result DX12GpuCommandContext::set_index_buffer(
        BufferHandle handle, IndexFormat format)
    {
        if (type() != CommandListType::Graphics)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Index buffer can only be set on graphics command lists.");
        }

        DX12BufferRecord* bufferRecord = nullptr;
        if (!m_bufferManager.try_get_record(handle, &bufferRecord) ||
            bufferRecord == nullptr)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Buffer record was not found for the given index buffer handle.");
        }
        if (bufferRecord->desc.type != BufferType::Index)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "The given buffer is not an index buffer.");
        }

        uint32_t resourceIndex = 0;
        Result result = resolve_slice_index(
            bufferRecord->defaultResources.size(), resourceIndex);
        if (!result)
        {
            return Result::fail(
                result.code,
                Severity::Error,
                "Failed to resolve index buffer resource for the current frame.");
        }

        DX12GpuResource& resource = bufferRecord->defaultResources[resourceIndex];
        D3D12_INDEX_BUFFER_VIEW indexBufferView{};
        indexBufferView.BufferLocation = resource.get_gpu_virtual_address();
        indexBufferView.SizeInBytes =
            static_cast<UINT>(resource.get_buffer_size());
        indexBufferView.Format =
            (format == IndexFormat::UInt16) ? DXGI_FORMAT_R16_UINT
                                            : DXGI_FORMAT_R32_UINT;
        m_commandList->IASetIndexBuffer(&indexBufferView);
        return Result::ok();
    }
    Result DX12GpuCommandContext::set_graphics_pipeline(PipelineStateHandle handle)
    {
        if (type() != CommandListType::Graphics)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Graphics pipeline can only be set on graphics command lists.");
        }

        DX12GraphicsPipelineRecord* pipelineRecord = nullptr;
        if (!m_pipelineManager.try_get_graphics_pipeline(handle, &pipelineRecord))
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Graphics pipeline was not found for the given handle.");
        }

        RootSignatureRecord* rootSignatureRecord = nullptr;
        if (!m_pipelineManager.try_get_root_signature(pipelineRecord->desc.rootSignatureHandle, &rootSignatureRecord))
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Root signature was not found for the given graphics pipeline.");
        }

        m_commandList->SetGraphicsRootSignature(rootSignatureRecord->rootSignature.Get());
        m_commandList->SetPipelineState(pipelineRecord->pipelineState.Get());
        return Result::ok();
    }
    Result DX12GpuCommandContext::set_compute_pipeline(PipelineStateHandle handle)
    {
        Result result = validate_root_binding_command_type(
            type(),
            "Compute pipeline can only be set on graphics or compute command contexts.");
        if (!result)
        {
            return result;
        }

        DX12ComputePipelineRecord* pipelineRecord = nullptr;
        if (!m_pipelineManager.try_get_compute_pipeline(handle, &pipelineRecord))
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Compute pipeline was not found for the given handle.");
        }

        RootSignatureRecord* rootSignatureRecord = nullptr;
        if (!m_pipelineManager.try_get_root_signature(pipelineRecord->desc.rootSignatureHandle, &rootSignatureRecord))
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Root signature was not found for the given compute pipeline.");
        }

        m_commandList->SetComputeRootSignature(rootSignatureRecord->rootSignature.Get());
        m_commandList->SetPipelineState(pipelineRecord->pipelineState.Get());
        return Result::ok();
    }
    Result DX12GpuCommandContext::set_32bit_constant(uint32_t rootParameterIndex, uint32_t value)
    {
        Result result = validate_root_binding_command_type(
            type(),
            "32-bit constants can only be bound on graphics or compute command contexts.");
        if (!result)
        {
            return result;
        }

        if (type() == CommandListType::Graphics)
        {
            m_commandList->SetGraphicsRoot32BitConstant(rootParameterIndex, value, 0);
        }
        else
        {
            m_commandList->SetComputeRoot32BitConstant(rootParameterIndex, value, 0);
        }

        return Result::ok();
    }
    Result DX12GpuCommandContext::set_cbv(uint32_t rootParameterIndex, BufferHandle handle)
    {
        Result result = validate_root_binding_command_type(
            type(),
            "CBV can only be bound on graphics or compute command contexts.");
        if (!result)
        {
            return result;
        }

        DX12GpuResource* resource = nullptr;
        result = resolve_root_descriptor_buffer(handle, &resource);
        if (!result)
        {
            return result;
        }

        const D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = resource->get_gpu_virtual_address();
        if (gpuAddress == D3D12_GPU_VIRTUAL_ADDRESS_NULL)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "GPU virtual address for CBV buffer was not found.");
        }

        if (type() == CommandListType::Graphics)
        {
            m_commandList->SetGraphicsRootConstantBufferView(rootParameterIndex, gpuAddress);
        }
        else
        {
            m_commandList->SetComputeRootConstantBufferView(rootParameterIndex, gpuAddress);
        }

        return Result::ok();
    }
    Result DX12GpuCommandContext::set_srv(uint32_t rootParameterIndex, BufferHandle handle)
    {
        Result result = validate_root_binding_command_type(
            type(),
            "SRV can only be bound on graphics or compute command contexts.");
        if (!result)
        {
            return result;
        }

        DX12GpuResource* resource = nullptr;
        result = resolve_root_descriptor_buffer(handle, &resource);
        if (!result)
        {
            return result;
        }

        const D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = resource->get_gpu_virtual_address();
        if (gpuAddress == D3D12_GPU_VIRTUAL_ADDRESS_NULL)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "GPU virtual address for SRV buffer was not found.");
        }

        if (type() == CommandListType::Graphics)
        {
            m_commandList->SetGraphicsRootShaderResourceView(rootParameterIndex, gpuAddress);
        }
        else
        {
            m_commandList->SetComputeRootShaderResourceView(rootParameterIndex, gpuAddress);
        }

        return Result::ok();
    }
    Result DX12GpuCommandContext::set_uav(uint32_t rootParameterIndex, BufferHandle handle)
    {
        Result result = validate_root_binding_command_type(
            type(),
            "UAV can only be bound on graphics or compute command contexts.");
        if (!result)
        {
            return result;
        }

        DX12GpuResource* resource = nullptr;
        result = resolve_root_descriptor_buffer(handle, &resource);
        if (!result)
        {
            return result;
        }

        const D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = resource->get_gpu_virtual_address();
        if (gpuAddress == D3D12_GPU_VIRTUAL_ADDRESS_NULL)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "GPU virtual address for UAV buffer was not found.");
        }

        if (type() == CommandListType::Graphics)
        {
            m_commandList->SetGraphicsRootUnorderedAccessView(rootParameterIndex, gpuAddress);
        }
        else
        {
            m_commandList->SetComputeRootUnorderedAccessView(rootParameterIndex, gpuAddress);
        }

        return Result::ok();
    }
    Result DX12GpuCommandContext::set_graphics_descriptor_table(uint32_t rootParameterIndex, ViewHandle handle)
    {
        if (type() != CommandListType::Graphics)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Graphics descriptor tables can only be set on graphics command lists.");
        }

        DX12ViewRecord* viewRecord = nullptr;
        if (!m_viewManager.try_get_record(handle, &viewRecord))
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "View record was not found for the given handle.");
        }
        uint32_t descriptorIndex = 0;
        Result result = resolve_slice_index(viewRecord->defaultTableIds.size(), descriptorIndex);
        if (!result)
        {
            return Result::fail(
                result.code,
                Severity::Error,
                "Failed to resolve descriptor table index for the current frame.");
        }

        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = m_descriptorAllocator.get_gpu_handle(viewRecord->defaultTableIds[descriptorIndex]);
        m_commandList->SetGraphicsRootDescriptorTable(rootParameterIndex, gpuHandle);
        return Result::ok();
    }
    Result DX12GpuCommandContext::dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
    {
        Result result = validate_root_binding_command_type(
            type(),
            "Dispatch can only be issued on graphics or compute command contexts.");
        if (!result)
        {
            return result;
        }

        m_commandList->Dispatch(groupCountX, groupCountY, groupCountZ);
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
            uint32_t descriptorIndex = 0;
            Result result = resolve_slice_index(rtvRecord->defaultTableIds.size(), descriptorIndex);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to resolve render target descriptor index for the current frame.");
            }
            rtvHandles[i] = m_descriptorAllocator.get_cpu_handle(rtvRecord->defaultTableIds[descriptorIndex]);
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
            uint32_t descriptorIndex = 0;
            Result result = resolve_slice_index(dsvRecord->defaultTableIds.size(), descriptorIndex);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to resolve depth stencil descriptor index for the current frame.");
            }
            dsvHandle = m_descriptorAllocator.get_cpu_handle(dsvRecord->defaultTableIds[descriptorIndex]);
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
    Result DX12GpuCommandContext::execute_indexed_indirect(
        BufferHandle commandBufferHandle,
        BufferHandle commandCountBufferHandle,
        uint32_t maxCommandCount)
    {
        if (type() != CommandListType::Graphics)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "ExecuteIndirect can only be issued on a graphics command context.");
        }
        if (!m_drawIndexedCommandSignature)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "Draw indexed command signature is not initialized.");
        }

        DX12GpuResource* commandResource = nullptr;
        Result result = resolve_default_buffer(commandBufferHandle, 0, &commandResource);
        if (!result)
        {
            return result;
        }

        DX12GpuResource* commandCountResource = nullptr;
        result = resolve_default_buffer(commandCountBufferHandle, 0, &commandCountResource);
        if (!result)
        {
            return result;
        }

        m_commandList->ExecuteIndirect(
            m_drawIndexedCommandSignature.Get(),
            maxCommandCount,
            commandResource->get_resource(),
            0,
            commandCountResource->get_resource(),
            0);
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
    Result DX12GpuCommandContext::create_draw_indexed_command_signature(
        ID3D12Device& device)
    {
        D3D12_INDIRECT_ARGUMENT_DESC argumentDesc{};
        argumentDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

        D3D12_COMMAND_SIGNATURE_DESC signatureDesc{};
        signatureDesc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
        signatureDesc.NumArgumentDescs = 1;
        signatureDesc.pArgumentDescs = &argumentDesc;

        HRESULT hr = device.CreateCommandSignature(
            &signatureDesc,
            nullptr,
            IID_PPV_ARGS(m_drawIndexedCommandSignature.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
        {
            return Result::fail(
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                "Failed to create draw indexed command signature.");
        }

        return Result::ok();
    }
    Result DX12GpuCommandContext::create_timestamp_resources(
        ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type)
    {
        if (type == D3D12_COMMAND_LIST_TYPE_COPY)
        {
            return Result::ok();
        }

        D3D12_QUERY_HEAP_DESC queryHeapDesc{};
        queryHeapDesc.Count = k_maxTimestampQueryCount;
        queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        HRESULT hr = device.CreateQueryHeap(
            &queryHeapDesc,
            IID_PPV_ARGS(m_timestampQueryHeap.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
        {
            return Result::fail(
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                "Failed to create timestamp query heap.");
        }

        D3D12_HEAP_PROPERTIES heapProperties{};
        heapProperties.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC resourceDesc{};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Width =
            static_cast<UINT64>(k_maxTimestampQueryCount) * sizeof(uint64_t);
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
        resourceDesc.SampleDesc = { 1, 0 };
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        hr = device.CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(m_timestampReadbackBuffer.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
        {
            return Result::fail(
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                "Failed to create timestamp readback buffer.");
        }

        void* mappedData = nullptr;
        const D3D12_RANGE readRange{ 0, 0 };
        hr = m_timestampReadbackBuffer->Map(0, &readRange, &mappedData);
        if (FAILED(hr) || mappedData == nullptr)
        {
            return Result::fail(
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                "Failed to map timestamp readback buffer.");
        }
        m_timestampReadbackMappedData = static_cast<std::byte*>(mappedData);
        std::memset(
            m_timestampReadbackMappedData,
            0,
            static_cast<size_t>(resourceDesc.Width));
        return Result::ok();
    }
    Result DX12GpuCommandContext::resolve_slice_index(size_t sliceCount, uint32_t& outIndex) const
    {
        if (sliceCount == 0)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Slice count must be greater than 0.");
        }

        // 単一実体のリソースは全フレームで index 0 を共有し、frame-local 実体だけ m_frameIndex で切り替える。
        if (sliceCount == 1)
        {
            outIndex = 0;
            return Result::ok();
        }

        // スワップチェインのような外部リソースは、フレームリング数とは別の実体数を持つことがあります。
        // Present パスでは m_frameIndex に「現在の back buffer index」が渡されるため、
        // その index が有効範囲内ならそのまま使います。
        if (m_frameIndex < sliceCount)
        {
            outIndex = m_frameIndex;
            return Result::ok();
        }

        if (sliceCount != m_bufferCount)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Slice count must be 1, match the global buffer count, or contain the current frame index.");
        }
        return Result::fail(
            Code::InvalidArgument,
            Severity::Error,
            "Frame index is out of range for the requested resource slice.");
    }
    Result DX12GpuCommandContext::resolve_root_descriptor_buffer(BufferHandle handle, DX12GpuResource** outResource) const
    {
        *outResource = nullptr;

        DX12BufferRecord* record = nullptr;
        if (!m_bufferManager.try_get_record(handle, &record) || record == nullptr)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Buffer record was not found for the given handle.");
        }

        if (!record->defaultResources.empty())
        {
            uint32_t resourceIndex = 0;
            Result result = resolve_slice_index(record->defaultResources.size(), resourceIndex);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to resolve default buffer slice index for root descriptor binding.");
            }

            *outResource = const_cast<DX12GpuResource*>(&record->defaultResources[resourceIndex]);
            return Result::ok();
        }

        if (!record->uploadResources.empty())
        {
            uint32_t resourceIndex = 0;
            Result result = resolve_slice_index(record->uploadResources.size(), resourceIndex);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to resolve upload buffer slice index for root descriptor binding.");
            }

            *outResource = const_cast<DX12GpuResource*>(&record->uploadResources[resourceIndex]);
            return Result::ok();
        }

        return Result::fail(
            Code::NotFound,
            Severity::Error,
            "No bindable buffer resource was found for the given handle.");
    }
    Result DX12GpuCommandContext::resolve_upload_buffer(BufferHandle handle, uint32_t resourceIndex, DX12GpuResource** outResource) const
    {
        *outResource = nullptr;

        DX12BufferRecord* record = nullptr;
        if (!m_bufferManager.try_get_record(handle, &record) || record == nullptr)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Source buffer record was not found.");
        }
        if (resourceIndex >= record->uploadResources.size())
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Source upload resource index is out of range.");
        }

        *outResource = const_cast<DX12GpuResource*>(&record->uploadResources[resourceIndex]);
        return Result::ok();
    }
    Result DX12GpuCommandContext::resolve_default_buffer(BufferHandle handle, uint32_t resourceIndex, DX12GpuResource** outResource) const
    {
        *outResource = nullptr;

        DX12BufferRecord* record = nullptr;
        if (!m_bufferManager.try_get_record(handle, &record) || record == nullptr)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Destination buffer record was not found.");
        }
        if (resourceIndex >= record->defaultResources.size())
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Destination default resource index is out of range.");
        }

        *outResource = const_cast<DX12GpuResource*>(&record->defaultResources[resourceIndex]);
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

        if (commandLists.empty())
        {
            return Result::ok();
        }

        m_commandQueue->ExecuteCommandLists(
            static_cast<UINT>(commandLists.size()), commandLists.data());
        return Result::ok();
    }
    Result DX12GpuCommandQueue::signal(uint64_t* outFenceValue)
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

        if (outFenceValue != nullptr)
        {
            *outFenceValue = fence;
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
    Result DX12GpuCommandQueue::wait_for_fence(uint64_t fenceValue)
    {
        if (!m_fence || !m_fenceEvent)
        {
            return Result::fail(
                Code::InternalError,
                Severity::Fatal,
                "Fence or Fence event is not initialized.");
        }
        if (m_fence->GetCompletedValue() < fenceValue)
        {
            m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent);
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
    Result DX12GpuCommandQueue::get_timestamp_frequency(
        uint64_t& outFrequency) const
    {
        outFrequency = 0;
        if (!m_commandQueue)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "CommandQueue is not initialized.");
        }

        HRESULT hr = m_commandQueue->GetTimestampFrequency(&outFrequency);
        if (FAILED(hr))
        {
            return Result::fail(
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                "Failed to query command queue timestamp frequency.");
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
        m_type = convert_command_list_type(type);
        return Result::ok();
    }
    Result DX12CommandPool::get_command_context(CommandListType type, commandContextLease& outContext)
    {
        switch (type)
        {
        case Cue::RHI::CommandListType::Graphics:
        {
            std::lock_guard lock(m_graphicsPoolMutex);
            // グラフィックスコマンドコンテキストをプールから取得
            auto context = m_graphicsContextPool.acquire();
            outContext = commandContextLease(
                context.release(),
                [](ICommandContext* raw) {delete raw; });
        }
            break;
        case Cue::RHI::CommandListType::Compute:
        {
            std::lock_guard lock(m_computePoolMutex);
            // コンピュートコマンドコンテキストをプールから取得
                auto context = m_computeContextPool.acquire();
                outContext = commandContextLease(
                    context.release(),
                    [](ICommandContext* raw) {delete raw; });
        }
            break;
        case Cue::RHI::CommandListType::Copy:
        {
            std::lock_guard lock(m_copyPoolMutex);
            // コピーコマンドコンテキストをプールから取得
                auto context = m_copyContextPool.acquire();
                outContext = commandContextLease(
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
    Result DX12CommandPool::return_command_context(commandContextLease& context)
    {
        if (!context)
        {
            return Result::ok();
        }

        Result waitResult = context->wait_for_pending_fence();
        if (!waitResult)
        {
            return waitResult;
        }

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
    Result DX12QueuePool::get_queue_context(CommandListType type, queueContextLease& outContext)
    {
        switch (type)
        {
        case Cue::RHI::CommandListType::Graphics:
        {
            std::lock_guard lock(m_graphicsPoolMutex);
            // グラフィックスコマンドキューコンテキストをプールから取得
            auto context = m_graphicsQueuePool.acquire();
            outContext = queueContextLease(
                context.release(),
                [](IQueueContext* raw) {delete raw; });
        }
            break;
        case Cue::RHI::CommandListType::Compute:
        {
            std::lock_guard lock(m_computePoolMutex);
            // コンピュートコマンドキューコンテキストをプールから取得
                auto context = m_computeQueuePool.acquire();
                outContext = queueContextLease(
                    context.release(),
                    [](IQueueContext* raw) {delete raw; });
        }
            break;
        case Cue::RHI::CommandListType::Copy:
        {
            std::lock_guard lock(m_copyPoolMutex);
            // コピーコマンドキューコンテキストをプールから取得
                auto context = m_copyQueuePool.acquire();
                outContext = queueContextLease(
                    context.release(),
                    [](IQueueContext* raw) {delete raw; });
        }
            break;
        default:
            break;
        }
        return Result::ok();
    }
    Result DX12QueuePool::return_queue_context(queueContextLease& context)
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
