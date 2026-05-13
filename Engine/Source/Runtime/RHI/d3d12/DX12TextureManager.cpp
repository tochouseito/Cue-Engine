#include "DX12TextureManager.h"

// === C++ includes ===
#include <algorithm>
#include <cstring>

namespace Cue::RHI::DX12
{
    Result DX12TextureManager::validate_texture_desc(const TextureDesc& desc) const
    {
        switch (desc.kind)
        {
        case TextureKind::Default:
            CUE_ASSERT_MSG(desc.bufferCount > 0, "Default texture must have at least one buffer.");
            CUE_ASSERT_MSG(desc.width > 0, "Texture width must be greater than 0.");
            CUE_ASSERT_MSG(desc.height > 0, "Texture height must be greater than 0.");
            break;
        case TextureKind::RenderTarget:
            CUE_ASSERT_MSG(desc.bufferCount > 0, "Default texture must have at least one buffer.");
            CUE_ASSERT_MSG(desc.width > 0, "Texture width must be greater than 0.");
            CUE_ASSERT_MSG(desc.height > 0, "Texture height must be greater than 0.");
            break;
        case TextureKind::DepthStencil:
            CUE_ASSERT_MSG(desc.bufferCount > 0, "Default texture must have at least one buffer.");
            CUE_ASSERT_MSG(desc.width > 0, "Texture width must be greater than 0.");
            CUE_ASSERT_MSG(desc.height > 0, "Texture height must be greater than 0.");
            CUE_ASSERT_MSG(desc.format == ColorFormat::D24_UNorm_S8_UInt, "DepthStencil texture must use D24_UNorm_S8_UInt format.");
            break;
        default:
            break;
        }

        return Result::ok();
    }

    Result DX12TextureManager::create_default_resource(const TextureDesc& desc,
        D3D12_RESOURCE_STATES initialState,
        DX12GpuResource& outResource) const
    {
        D3D12_RESOURCE_DESC resourceDesc = {};
        resourceDesc.Width = desc.width;
        resourceDesc.Height = desc.height;
        resourceDesc.MipLevels = desc.mipLevels;
        resourceDesc.DepthOrArraySize = desc.arraySize;
        resourceDesc.SampleDesc.Count = desc.sampleCount;
        resourceDesc.Format = convert_color_format(desc.format);
        if (desc.kind == TextureKind::DepthStencil &&
            desc.format == ColorFormat::D24_UNorm_S8_UInt)
        {
            resourceDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
        }
        resourceDesc.Dimension =
            desc.type == TextureType::Texture2D ? D3D12_RESOURCE_DIMENSION_TEXTURE2D :
            D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        resourceDesc.Flags =
            desc.kind == TextureKind::RenderTarget ? D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET :
            desc.kind == TextureKind::DepthStencil ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL :
            D3D12_RESOURCE_FLAG_NONE;

        D3D12_CLEAR_VALUE clearValue = {};
        const D3D12_CLEAR_VALUE* clearValuePtr = nullptr;
        if (desc.kind == TextureKind::RenderTarget)
        {
            clearValue.Format = convert_color_format(desc.format);
            clearValue.Color[0] = desc.clearColor[0];
            clearValue.Color[1] = desc.clearColor[1];
            clearValue.Color[2] = desc.clearColor[2];
            clearValue.Color[3] = desc.clearColor[3];
            clearValuePtr = &clearValue;
        }
        else if (desc.kind == TextureKind::DepthStencil)
        {
            clearValue.Format = convert_color_format(desc.format);
            clearValue.DepthStencil.Depth = desc.clearDepth;
            clearValue.DepthStencil.Stencil = desc.clearStencil;
            clearValuePtr = &clearValue;
        }

        D3D12_HEAP_PROPERTIES heapProperties = {};
        heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

        std::wstring name = L"";
        PAL::Win::utf8_to_wide(desc.name, &name);
        return outResource.create(
            *m_renderDevice.get_d3d12_device(),
            heapProperties,
            D3D12_HEAP_FLAG_NONE,
            resourceDesc,
            initialState,
            clearValuePtr,
            name);
    }

    Result DX12TextureManager::upload_initial_data(DX12GpuResource& resource,
        std::span<const TextureSubresourceData> initialData) const
    {
        ID3D12Device* device = m_renderDevice.get_d3d12_device();
        if (device == nullptr)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "D3D12 device is not initialized.");
        }

        ID3D12Resource* textureResource = resource.get_resource();
        if (textureResource == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Texture resource is not initialized.");
        }

        const D3D12_RESOURCE_DESC resourceDesc = textureResource->GetDesc();
        const uint32_t subresourceCount =
            static_cast<uint32_t>(initialData.size());
        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(subresourceCount);
        std::vector<uint32_t> numRows(subresourceCount);
        std::vector<uint64_t> rowSizesInBytes(subresourceCount);
        uint64_t totalUploadSize = 0;
        device->GetCopyableFootprints(
            &resourceDesc,
            0,
            subresourceCount,
            0,
            layouts.data(),
            numRows.data(),
            rowSizesInBytes.data(),
            &totalUploadSize);

        D3D12_HEAP_PROPERTIES uploadHeapProperties = {};
        uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC uploadDesc = {};
        uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        uploadDesc.Width = totalUploadSize;
        uploadDesc.Height = 1;
        uploadDesc.DepthOrArraySize = 1;
        uploadDesc.MipLevels = 1;
        uploadDesc.SampleDesc.Count = 1;
        uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        DX12GpuResource uploadResource{};
        Result result = uploadResource.create(
            *device,
            uploadHeapProperties,
            D3D12_HEAP_FLAG_NONE,
            uploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            L"TextureUpload");
        if (!result)
        {
            return result;
        }

        result = uploadResource.map_persistent();
        if (!result)
        {
            uploadResource.destroy();
            return result;
        }

        std::byte* mappedData = uploadResource.mapped_data();
        for (uint32_t subresourceIndex = 0; subresourceIndex < subresourceCount; ++subresourceIndex)
        {
            const TextureSubresourceData& subresource = initialData[subresourceIndex];
            if (subresource.data == nullptr)
            {
                uploadResource.destroy();
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Texture initial data contains a null pointer.");
            }

            const D3D12_SUBRESOURCE_FOOTPRINT& footprint =
                layouts[subresourceIndex].Footprint;
            const uint32_t rowCount = numRows[subresourceIndex];
            const uint32_t depthCount =
                footprint.Depth == 0 ? 1u : footprint.Depth;
            const uint64_t expectedSliceSize =
                static_cast<uint64_t>(subresource.rowPitch) * rowCount;
            if (subresource.rowPitch == 0 ||
                subresource.slicePitch < expectedSliceSize ||
                subresource.dataSize <
                static_cast<uint64_t>(subresource.slicePitch) * depthCount)
            {
                uploadResource.destroy();
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Texture initial data pitch is invalid.");
            }

            std::byte* destinationBase = mappedData + layouts[subresourceIndex].Offset;
            for (uint32_t depthIndex = 0; depthIndex < depthCount; ++depthIndex)
            {
                const std::byte* sourceSlice =
                    subresource.data +
                    static_cast<uint64_t>(subresource.slicePitch) * depthIndex;
                std::byte* destinationSlice =
                    destinationBase +
                    static_cast<uint64_t>(footprint.RowPitch) * rowCount * depthIndex;
                for (uint32_t rowIndex = 0; rowIndex < rowCount; ++rowIndex)
                {
                    const uint64_t sourceOffset =
                        static_cast<uint64_t>(subresource.rowPitch) * rowIndex;
                    const uint64_t destinationOffset =
                        static_cast<uint64_t>(footprint.RowPitch) * rowIndex;
                    std::memcpy(
                        destinationSlice + destinationOffset,
                        sourceSlice + sourceOffset,
                        rowSizesInBytes[subresourceIndex]);
                }
            }
        }

        comPtr<ID3D12CommandAllocator> commandAllocator = nullptr;
        HRESULT hr = device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&commandAllocator));
        if (FAILED(hr))
        {
            uploadResource.destroy();
            return Result::fail(
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                "Failed to create command allocator for texture upload.");
        }

        comPtr<ID3D12GraphicsCommandList> commandList = nullptr;
        hr = device->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            commandAllocator.Get(),
            nullptr,
            IID_PPV_ARGS(&commandList));
        if (FAILED(hr))
        {
            uploadResource.destroy();
            return Result::fail(
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                "Failed to create command list for texture upload.");
        }

        for (uint32_t subresourceIndex = 0; subresourceIndex < subresourceCount; ++subresourceIndex)
        {
            D3D12_TEXTURE_COPY_LOCATION destination = {};
            destination.pResource = textureResource;
            destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            destination.SubresourceIndex = subresourceIndex;

            D3D12_TEXTURE_COPY_LOCATION source = {};
            source.pResource = uploadResource.get_resource();
            source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            source.PlacedFootprint = layouts[subresourceIndex];
            commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        }

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = textureResource;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter =
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        commandList->ResourceBarrier(1, &barrier);

        hr = commandList->Close();
        if (FAILED(hr))
        {
            uploadResource.destroy();
            return Result::fail(
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                "Failed to close command list for texture upload.");
        }

        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        comPtr<ID3D12CommandQueue> commandQueue = nullptr;
        hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue));
        if (FAILED(hr))
        {
            uploadResource.destroy();
            return Result::fail(
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                "Failed to create command queue for texture upload.");
        }

        ID3D12CommandList* commandLists[] = { commandList.Get() };
        commandQueue->ExecuteCommandLists(1, commandLists);

        comPtr<ID3D12Fence> fence = nullptr;
        hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
        if (FAILED(hr))
        {
            uploadResource.destroy();
            return Result::fail(
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                "Failed to create fence for texture upload.");
        }

        const uint64_t fenceValue = 1;
        hr = commandQueue->Signal(fence.Get(), fenceValue);
        if (FAILED(hr))
        {
            uploadResource.destroy();
            return Result::fail(
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                "Failed to signal fence for texture upload.");
        }

        HANDLE fenceEvent =
            CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
        if (fenceEvent == nullptr)
        {
            uploadResource.destroy();
            return Result::fail(
                PAL::Win::convert_hresult_code(HRESULT_FROM_WIN32(::GetLastError())),
                Severity::Error,
                "Failed to create fence event for texture upload.");
        }

        if (fence->GetCompletedValue() < fenceValue)
        {
            hr = fence->SetEventOnCompletion(fenceValue, fenceEvent);
            if (FAILED(hr))
            {
                CloseHandle(fenceEvent);
                uploadResource.destroy();
                return Result::fail(
                    PAL::Win::convert_hresult_code(hr),
                    Severity::Error,
                    "Failed to wait for texture upload fence.");
            }
            WaitForSingleObject(fenceEvent, INFINITE);
        }

        CloseHandle(fenceEvent);
        uploadResource.destroy();
        resource.set_current_state(
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        return Result::ok();
    }

    Result DX12TextureManager::create_texture(const TextureDesc& desc, TextureHandle& out)
    {
        DX12TextureRecord record{};
        Result result = validate_texture_desc(desc);
        if (!result)
        {
            return result;
        }

        for (uint32_t i = 0; i < desc.bufferCount; ++i)
        {
            DX12GpuResource resource{};
            result = create_default_resource(
                desc,
                D3D12_RESOURCE_STATE_COMMON,
                resource);
            if (!result)
            {
                return result;
            }
            record.defaultResources.emplace_back(std::move(resource));
        }

        // レコードの保存
        record.desc = desc;
        TextureHandle handle = m_textureRegistry.create(record);
        if (!desc.name.empty())
        {
            m_nameToHandlesMap[Core::fnv1a64(desc.name)] = handle;
        }

        out = std::move(handle);

        return Result::ok();
    }

    Result DX12TextureManager::create_texture(const TextureDesc& desc,
        std::span<const TextureSubresourceData> initialData,
        TextureHandle& out)
    {
        DX12TextureRecord record{};
        Result result = validate_texture_desc(desc);
        if (!result)
        {
            return result;
        }

        if (desc.kind != TextureKind::Default)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Initial texture upload is supported only for default textures.");
        }

        const uint32_t expectedSubresourceCount =
            static_cast<uint32_t>(desc.mipLevels) *
            std::max<uint32_t>(desc.arraySize, 1u);
        if (initialData.size() != expectedSubresourceCount)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Texture initial data subresource count does not match the texture description.");
        }

        for (uint32_t i = 0; i < desc.bufferCount; ++i)
        {
            DX12GpuResource resource{};
            result = create_default_resource(
                desc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                resource);
            if (!result)
            {
                return result;
            }

            result = upload_initial_data(resource, initialData);
            if (!result)
            {
                resource.destroy();
                return result;
            }

            record.defaultResources.emplace_back(std::move(resource));
        }

        record.descriptorTableId = m_descriptorAllocator.allocate(TableKind::Textures);
        if (!record.descriptorTableId.valid())
        {
            for (DX12GpuResource& resource : record.defaultResources)
            {
                resource.destroy();
            }
            return Result::fail(
                Code::OutOfMemory,
                Severity::Error,
                "Failed to allocate texture descriptor table slot.");
        }

        result = m_descriptorAllocator.create_srv_texture_2d(
            record.descriptorTableId,
            &record.defaultResources[0],
            convert_color_format(desc.format),
            0,
            desc.mipLevels);
        if (!result)
        {
            m_descriptorAllocator.free_table(record.descriptorTableId);
            for (DX12GpuResource& resource : record.defaultResources)
            {
                resource.destroy();
            }
            return result;
        }

        record.desc = desc;
        TextureHandle handle = m_textureRegistry.create(record);
        if (!desc.name.empty())
        {
            m_nameToHandlesMap[Core::fnv1a64(desc.name)] = handle;
        }

        out = std::move(handle);
        return Result::ok();
    }
    Result DX12TextureManager::destroy_texture(TextureHandle handle)
    {
        // ハンドルの解決と、破棄前に全リソースが解放可能かを確認する
        Result result = Result::ok();

        const bool found = m_textureRegistry.with(handle, [this, handle, &result](DX12TextureRecord&
            record)
            {
                for (const DX12GpuResource& resource : record.defaultResources)
                {
                    if (resource.is_in_use())
                    {
                        result = Result::fail(
                            Code::AccessDenied,
                            Severity::Error,
                            "Failed to destroy buffer because one or more resources are still in use.");
                        return;
                    }
                }

                // 名前テーブルを先に外して、破棄後に古い名前引きを残さない
                if (!record.desc.name.empty())
                {
                    const Core::ResourceNameId nameId = Core::fnv1a64(record.desc.name);
                    const auto it = m_nameToHandlesMap.find(nameId);
                    if (it != m_nameToHandlesMap.end() && it->second == handle)
                    {
                        m_nameToHandlesMap.erase(it);
                    }
                }

                // 実リソースを順に破棄する
                for (DX12GpuResource& resource : record.defaultResources)
                {
                    if (!resource.destroy())
                    {
                        result = Result::fail(
                            Code::AccessDenied,
                            Severity::Error,
                            "Failed to destroy buffer because one or more resources are still in use.");
                        return;
                    }
                }

                if (record.descriptorTableId.valid())
                {
                    m_descriptorAllocator.free_table(record.descriptorTableId);
                    record.descriptorTableId = {};
                }
            });

        if (!found)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Failed to destroy buffer because the handle was not found.");
        }

        if (!result)
        {
            return result;
        }

        // 論理レコードを削除してハンドルを無効化する
        if (!m_textureRegistry.destroy(handle))
        {
            return Result::fail(
                Code::InternalError,
                Severity::Error,
                "Failed to remove texture record from registry.");
        }

        return Result::ok();
    }

    Result DX12TextureManager::get_texture_descriptor_index(TextureHandle handle,
        uint32_t& outIndex)
    {
        outIndex = 0;

        DX12TextureRecord* record = nullptr;
        if (!try_get_record(handle, &record))
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Texture not found for the given handle.");
        }

        if (!record->descriptorTableId.valid())
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "Texture descriptor table slot is not allocated.");
        }

        outIndex = record->descriptorTableId.index;
        return Result::ok();
    }

    Result DX12TextureManager::get_texture(std::string_view name, TextureHandle& out)
    {
        if (m_nameToHandlesMap.contains(Core::fnv1a64(name)))
        {
            out = m_nameToHandlesMap[Core::fnv1a64(name)];
            return Result::ok();
        }
        else
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Texture not found for the given name.");
        }
    }

    bool DX12TextureManager::try_get_record(TextureHandle handle, DX12TextureRecord** outRecord)
    {
        // ハンドルの解決とレコードの取得
        *outRecord = nullptr;
        *outRecord = m_textureRegistry.ref_get(handle);
        return *outRecord != nullptr;
    }

    Result DX12TextureManager::register_external_texture(DX12TextureRecord& record, TextureHandle& out)
    {
        // レコードの保存
        std::string name = record.desc.name;
        TextureHandle handle = m_textureRegistry.create(record);
        if (!name.empty())
        {
            m_nameToHandlesMap[Core::fnv1a64(name)] = handle;
        }

        out = std::move(handle);

        return Result::ok();
    }
}
