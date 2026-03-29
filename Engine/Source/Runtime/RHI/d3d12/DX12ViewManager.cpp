#include "DX12ViewManager.h"
#include "DX12GpuCommand.h"

namespace Cue::RHI::DX12
{
    namespace
    {
        TableKind convert_buffer_kind(BufferKind kind)
        {
            switch (kind)
            {
            case BufferKind::Texture:
                return TableKind::Textures;
            case BufferKind::Buffer:
                return TableKind::Buffers;
            case BufferKind::RenderTarget:
                return TableKind::RenderTargets;
            case BufferKind::DepthStencil:
                return TableKind::DepthStencils;
            default:
                return TableKind::Buffers;
            }
        }
    }

    Result DX12ViewManager::create_view(const ViewDesc& desc, ViewHandle& out)
    {
        // 1) view 記述から対象 resource を解決し、frame-buffering 数ぶんの descriptor を作れる状態か検証します。
        DX12ViewRecord record{};
        record.desc = desc;

        Result result = Result::ok();
        switch (desc.type)
        {
        case ViewType::ConstantBuffer:
        case ViewType::ShaderResourceBuffer:
        case ViewType::ShaderResourceRawBuffer:
        case ViewType::UnorderedAccessBuffer:
        case ViewType::UnorderedAccessRawBuffer:
        {
            DX12BufferRecord* bufferRecord = nullptr;
            if (!m_bufferManager.try_get_record(desc.bufferHandle, &bufferRecord) || bufferRecord == nullptr)
            {
                return Result::fail(
                    Code::NotFound,
                    Severity::Error,
                    "Buffer not found for the given view description.");
            }

            for (DX12GpuResource& resource : bufferRecord->defaultResources)
            {
                TableID tableId = m_descriptorAllocator.allocate(convert_buffer_kind(desc.bufferKind));
                switch (desc.type)
                {
                case ViewType::ConstantBuffer:
                    result = m_descriptorAllocator.create_cbv(tableId, &resource, desc.byteOffset, desc.byteSize);
                    break;
                case ViewType::ShaderResourceBuffer:
                    result = m_descriptorAllocator.create_srv_buffer(tableId, &resource, desc.firstElement, desc.numElements, desc.structureByteStride);
                    break;
                case ViewType::ShaderResourceRawBuffer:
                    result = m_descriptorAllocator.create_srv_raw_buffer(tableId, &resource, desc.firstElement, desc.numElements);
                    break;
                case ViewType::UnorderedAccessBuffer:
                    result = m_descriptorAllocator.create_uav_buffer(tableId, &resource, desc.firstElement, desc.numElements, desc.structureByteStride);
                    break;
                case ViewType::UnorderedAccessRawBuffer:
                    result = m_descriptorAllocator.create_uav_raw_buffer(tableId, &resource, desc.firstElement, desc.numElements);
                    break;
                default:
                    break;
                }

                if (!result)
                {
                    return result;
                }
                record.defaultTableIds.emplace_back(tableId);
            }

            for (DX12GpuResource& resource : bufferRecord->uploadResources)
            {
                TableID tableId = m_descriptorAllocator.allocate(convert_buffer_kind(desc.bufferKind));
                switch (desc.type)
                {
                case ViewType::ConstantBuffer:
                    result = m_descriptorAllocator.create_cbv(tableId, &resource, desc.byteOffset, desc.byteSize);
                    break;
                case ViewType::ShaderResourceBuffer:
                    result = m_descriptorAllocator.create_srv_buffer(tableId, &resource, desc.firstElement, desc.numElements, desc.structureByteStride);
                    break;
                case ViewType::ShaderResourceRawBuffer:
                    result = m_descriptorAllocator.create_srv_raw_buffer(tableId, &resource, desc.firstElement, desc.numElements);
                    break;
                case ViewType::UnorderedAccessBuffer:
                    result = m_descriptorAllocator.create_uav_buffer(tableId, &resource, desc.firstElement, desc.numElements, desc.structureByteStride);
                    break;
                case ViewType::UnorderedAccessRawBuffer:
                    result = m_descriptorAllocator.create_uav_raw_buffer(tableId, &resource, desc.firstElement, desc.numElements);
                    break;
                default:
                    break;
                }

                if (!result)
                {
                    return result;
                }
                record.uploadTableIds.emplace_back(tableId);
            }
            break;
        }
        case ViewType::ShaderResourceTexture2D:
        case ViewType::UnorderedAccessTexture2D:
        case ViewType::RenderTarget:
        case ViewType::DepthStencil:
        {
            DX12TextureRecord* textureRecord = nullptr;
            if (!m_textureManager.try_get_record(desc.textureHandle, textureRecord) || textureRecord == nullptr)
            {
                return Result::fail(
                    Code::NotFound,
                    Severity::Error,
                    "Texture not found for the given view description.");
            }

            const bool usesSingleImportedRenderTargetSlot =
                textureRecord->imported && desc.type == ViewType::RenderTarget;
            for (size_t resourceIndex = 0; resourceIndex < textureRecord->defaultResources.size(); ++resourceIndex)
            {
                DX12GpuResource& resource = textureRecord->defaultResources[resourceIndex];
                TableID tableId = m_descriptorAllocator.allocate(convert_buffer_kind(desc.bufferKind));
                switch (desc.type)
                {
                case ViewType::ShaderResourceTexture2D:
                    result = m_descriptorAllocator.create_srv_texture_2d(
                        tableId,
                        &resource,
                        convert_color_format(desc.colorFormat),
                        desc.mipSlice,
                        desc.mipLevels);
                    break;
                case ViewType::UnorderedAccessTexture2D:
                    result = m_descriptorAllocator.create_uav_texture_2d(
                        tableId,
                        &resource,
                        convert_color_format(desc.colorFormat),
                        desc.mipSlice);
                    break;
                case ViewType::RenderTarget:
                    result = m_descriptorAllocator.create_rtv(
                        tableId,
                        &resource,
                        convert_color_format(desc.colorFormat),
                        desc.mipSlice);
                    break;
                case ViewType::DepthStencil:
                    result = m_descriptorAllocator.create_dsv(
                        tableId,
                        &resource,
                        convert_dsv_format(desc.dsvFormat),
                        desc.mipSlice);
                    break;
                default:
                    break;
                }

                if (!result)
                {
                    return result;
                }
                record.defaultTableIds.emplace_back(tableId);

                // 1) imported swapchain の RTV は 1 スロットだけを持ち、実行時に current backbuffer へ張り直します。
                if (usesSingleImportedRenderTargetSlot)
                {
                    break;
                }
            }
            break;
        }
        default:
            return Result::fail(
                Code::Unsupported,
                Severity::Error,
                "Unsupported view type.");
        }

        // 2) view 実体を registry へ登録して、pass 実行時は frameIndex だけで descriptor を引けるようにします。
        ViewHandle handle = m_viewRegistry.create(record);
        if (!desc.name.empty())
        {
            m_nameToHandlesMap[Core::fnv1a64(desc.name)] = handle;
        }

        out = handle;
        return Result::ok();
    }

    Result DX12ViewManager::destroy_view(ViewHandle handle)
    {
        // 1) descriptor を先に解放して、view handle が消えたあとに GPU heap だけ残るのを防ぎます。
        DX12ViewRecord* record = m_viewRegistry.get(handle);
        if (!record)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "View not found for the given handle.");
        }

        for (const TableID tableId : record->defaultTableIds)
        {
            if (tableId.valid())
            {
                m_descriptorAllocator.free_table(tableId);
            }
        }
        for (const TableID tableId : record->uploadTableIds)
        {
            if (tableId.valid())
            {
                m_descriptorAllocator.free_table(tableId);
            }
        }

        // 2) 名前マップと registry を同期して、古い handle 参照を残しません。
        for (auto it = m_nameToHandlesMap.begin(); it != m_nameToHandlesMap.end(); ++it)
        {
            if (it->second == handle)
            {
                m_nameToHandlesMap.erase(it);
                break;
            }
        }
        if (!m_viewRegistry.destroy(handle))
        {
            return Result::fail(
                Code::InternalError,
                Severity::Error,
                "Failed to remove view from registry.");
        }

        return Result::ok();
    }

    Result DX12ViewManager::clear_render_target(
        ICommandContext& commandContext,
        ViewHandle handle,
        uint32_t frameIndex,
        const std::array<float, 4>& clearColor)
    {
        // 1) ViewHandle から frameIndex 対応の RTV descriptor と texture 実体を引き、clear 対象を backend 側で確定します。
        const DX12ViewRecord* viewRecord = nullptr;
        if (!try_get_record(handle, viewRecord) || viewRecord == nullptr)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "DX12 view record was not found for clear_render_target.");
        }
        if (viewRecord->desc.type != ViewType::RenderTarget)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "clear_render_target requires a render target view.");
        }

        TableID tableId{};
        if (!try_get_default_table(handle, frameIndex, tableId))
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Failed to resolve RTV descriptor table for clear_render_target.");
        }

        DX12TextureRecord* textureRecord = nullptr;
        if (!m_textureManager.try_get_record(viewRecord->desc.textureHandle, textureRecord) || textureRecord == nullptr)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Failed to resolve texture for clear_render_target.");
        }
        if (textureRecord->defaultResources.empty())
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "The texture for clear_render_target has no default resources.");
        }

        const uint32_t resourceIndex = frameIndex % static_cast<uint32_t>(textureRecord->defaultResources.size());
        DX12GpuResource& resource = textureRecord->defaultResources[resourceIndex];

        if (textureRecord->imported)
        {
            Result rtvResult = m_descriptorAllocator.create_rtv(
                tableId,
                &resource,
                convert_color_format(viewRecord->desc.colorFormat),
                viewRecord->desc.mipSlice);
            if (!rtvResult)
            {
                return rtvResult;
            }
        }

        // 2) clear 前に RTV state へ遷移して、DX12 の state 契約違反をここで吸収します。
        auto* dx12CommandContext = dynamic_cast<DX12GpuCommandContext*>(&commandContext);
        if (!dx12CommandContext)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "clear_render_target requires a DX12 command context.");
        }

        ID3D12GraphicsCommandList* commandList = dx12CommandContext->d3d12_command_list();
        if (!commandList)
        {
            return Result::fail(
                Code::InternalError,
                Severity::Error,
                "DX12 command list was null in clear_render_target.");
        }

        if (resource.current_state() != D3D12_RESOURCE_STATE_RENDER_TARGET)
        {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = resource.get_resource();
            barrier.Transition.StateBefore = resource.current_state();
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            commandList->ResourceBarrier(1, &barrier);
            resource.set_current_state(D3D12_RESOURCE_STATE_RENDER_TARGET);
        }

        // 3) 解決した RTV descriptor へ対して clear を記録し、pass 実装は色だけ渡せば済むようにします。
        const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_descriptorAllocator.get_cpu_handle(tableId);
        commandList->ClearRenderTargetView(rtvHandle, clearColor.data(), 0, nullptr);

        // 4) swapchain buffer は Present 前提で使うので、clear 後は PRESENT へ戻して次段の契約を揃えます。
        if (textureRecord->imported)
        {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = resource.get_resource();
            barrier.Transition.StateBefore = resource.current_state();
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            commandList->ResourceBarrier(1, &barrier);
            resource.set_current_state(D3D12_RESOURCE_STATE_PRESENT);
        }

        return Result::ok();
    }

    bool DX12ViewManager::try_get_record(ViewHandle handle, const DX12ViewRecord*& outRecord) const
    {
        // 1) backend 実行時だけ短期参照で record を引きます。
        outRecord = m_viewRegistry.get(handle);
        return outRecord != nullptr;
    }

    bool DX12ViewManager::try_get_default_table(ViewHandle handle, uint32_t frameIndex, TableID& outTable) const
    {
        // 1) frameIndex に対応する default descriptor を返し、buffered resource 解決を manager に集約します。
        outTable = {};
        const DX12ViewRecord* record = nullptr;
        if (!try_get_record(handle, record) || record == nullptr || record->defaultTableIds.empty())
        {
            return false;
        }

        const uint32_t index = frameIndex % static_cast<uint32_t>(record->defaultTableIds.size());
        outTable = record->defaultTableIds[index];
        return outTable.valid();
    }

    bool DX12ViewManager::try_get_upload_table(ViewHandle handle, uint32_t frameIndex, TableID& outTable) const
    {
        // 1) upload descriptor も同じく frameIndex から引けるようにして、CBV 更新経路と揃えます。
        outTable = {};
        const DX12ViewRecord* record = nullptr;
        if (!try_get_record(handle, record) || record == nullptr || record->uploadTableIds.empty())
        {
            return false;
        }

        const uint32_t index = frameIndex % static_cast<uint32_t>(record->uploadTableIds.size());
        outTable = record->uploadTableIds[index];
        return outTable.valid();
    }
}
