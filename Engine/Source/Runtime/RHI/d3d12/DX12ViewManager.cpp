#include "DX12ViewManager.h"

namespace Cue::RHI::DX12
{
    namespace
    {
        void free_table_ids(DescriptorAllocator& a_descriptorAllocator,
            std::vector<TableID>& a_ids) noexcept
        {
            for (const TableID& tableId : a_ids)
            {
                a_descriptorAllocator.free_table(tableId);
            }
            a_ids.clear();
        }

        TableKind convert_view_kind(ViewType type)
        {
            switch (type)
            {
            case ViewType::ConstantBuffer:
            case ViewType::ShaderResourceBuffer:
            case ViewType::ShaderResourceRawBuffer:
            case ViewType::UnorderedAccessBuffer:
            case ViewType::UnorderedAccessRawBuffer:
            case ViewType::ShaderResourceTexture2D:
            case ViewType::UnorderedAccessTexture2D:
                return TableKind::Buffers;
            case ViewType::RenderTarget:
                return TableKind::RenderTargets;
            case ViewType::DepthStencil:
                return TableKind::DepthStencils;
            default:
                return TableKind::Buffers; // デフォルトはバッファ用テーブルとする
            }
        }

        [[nodiscard]] bool supports_upload_buffer_view(ViewType type) noexcept
        {
            switch (type)
            {
            case ViewType::ConstantBuffer:
            case ViewType::ShaderResourceBuffer:
            case ViewType::ShaderResourceRawBuffer:
                return true;
            default:
                return false;
            }
        }
    }

    Result DX12ViewManager::create_view(const ViewDesc& desc, ViewHandle& out)
    {
        DX12ViewRecord record{};
        record.desc = desc;

        // View の作成
        Result result;
        switch (desc.bufferKind)
        {
        case BufferKind::Buffer:
        {
            // Buffer の実体を取得する
            DX12BufferRecord* bufferRecord = nullptr;
            if (!m_bufferManager.try_get_record(desc.bufferHandle, &bufferRecord))
            {
                return Result::fail(
                    Code::NotFound,
                    Severity::Error,
                    "Buffer not found for the given handle.");
            }
            // Default Resource
            for (auto& resource : bufferRecord->defaultResources)
            {
                result = create_view_impl(desc, resource, record.defaultTableIds);
                if (!result)
                {
                    free_table_ids(m_descriptorAllocator, record.defaultTableIds);
                    free_table_ids(m_descriptorAllocator, record.uploadTableIds);
                    return Result::fail(
                        Code::CreateFailed,
                        Severity::Error,
                        "Failed to create CBV for default resource.");
                }
            }
            // Upload Resource
            for (auto& resource : bufferRecord->uploadResources)
            {
                if (!supports_upload_buffer_view(desc.type))
                {
                    continue;
                }

                result = create_view_impl(desc, resource, record.uploadTableIds);
                if (!result)
                {
                    free_table_ids(m_descriptorAllocator, record.defaultTableIds);
                    free_table_ids(m_descriptorAllocator, record.uploadTableIds);
                    return Result::fail(
                        Code::CreateFailed,
                        Severity::Error,
                        "Failed to create CBV for upload resource.");
                }
            }
        }
            break;
        case BufferKind::Texture:
        {
            // Texture の実体を取得する
            DX12TextureRecord* textureRecord = nullptr;
            if (!m_textureManager.try_get_record(desc.textureHandle, &textureRecord))
            {
                return Result::fail(
                    Code::NotFound,
                    Severity::Error,
                    "Texture not found for the given handle.");
            }
            // Default Resource
            for (auto& resource : textureRecord->defaultResources)
            {
                result = create_view_impl(desc, resource, record.defaultTableIds);
                if (!result)
                {
                    free_table_ids(m_descriptorAllocator, record.defaultTableIds);
                    free_table_ids(m_descriptorAllocator, record.uploadTableIds);
                    return Result::fail(
                        Code::CreateFailed,
                        Severity::Error,
                        "Failed to create view for default resource.");
                }
            }
        }
            break;
        default:
            break;
        }

        // レジストリに登録する
        ViewHandle handle = m_viewRegistry.create(record);

        // 名前マップに登録する
        if (!desc.name.empty())
        {
            m_nameToHandlesMap[Core::fnv1a64(desc.name)] = handle;
        }

        out = handle;
        return Result::ok();
    }
    Result DX12ViewManager::destroy_view(ViewHandle handle)
    {
        // ハンドルの有効性を検査する
        if (!handle.valid())
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Invalid view handle.");
        }

        DX12ViewRecord* record = m_viewRegistry.ref_get(handle);
        if (record == nullptr)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "View not found for the given handle.");
        }

        free_table_ids(m_descriptorAllocator, record->defaultTableIds);
        free_table_ids(m_descriptorAllocator, record->uploadTableIds);

        // 名前マップから削除する
        for (auto it = m_nameToHandlesMap.begin(); it != m_nameToHandlesMap.end(); ++it)
        {
            if (it->second == handle)
            {
                m_nameToHandlesMap.erase(it);
                break;
            }
        }

        // レジストリから削除する
        if (!m_viewRegistry.destroy(handle))
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "View not found for the given handle.");
        }

        return Result::ok();
    }
    bool DX12ViewManager::try_get_record(ViewHandle handle, DX12ViewRecord** outRecord)
    {
        // ハンドルの解決とレコードの取得
        *outRecord = nullptr;
        *outRecord = m_viewRegistry.ref_get(handle);
        return *outRecord != nullptr;
    }
    Result DX12ViewManager::create_view_impl(const ViewDesc& desc, DX12GpuResource& resource, std::vector<TableID>& ids)
    {
        switch (desc.type)
        {
        case ViewType::ConstantBuffer:
        {
            // CBV を作成する
            TableID tableId = m_descriptorAllocator.allocate(convert_view_kind(desc.type));
            Result result = m_descriptorAllocator.create_cbv(tableId, &resource, desc.byteOffset, desc.byteSize);
            if (!result)
            {
                return Result::fail(
                    Code::CreateFailed,
                    Severity::Error,
                    "Failed to create CBV for the resource.");
            }
            ids.emplace_back(tableId);
            break;
        }
        case ViewType::ShaderResourceBuffer:
        case ViewType::ShaderResourceRawBuffer:
        {
            // SRV を作成する
            TableID tableId = m_descriptorAllocator.allocate(convert_view_kind(desc.type));
            Result result = m_descriptorAllocator.create_srv_buffer(tableId, &resource, desc.firstElement, desc.numElements, desc.structureByteStride);
            if (!result)
            {
                return Result::fail(
                    Code::CreateFailed,
                    Severity::Error,
                    "Failed to create SRV for the resource.");
            }
            ids.emplace_back(tableId);
            break;
        }
        case ViewType::UnorderedAccessBuffer:
        {
            // UAV を作成する
            TableID tableId = m_descriptorAllocator.allocate(convert_view_kind(desc.type));
            Result result = m_descriptorAllocator.create_uav_buffer(tableId, &resource, desc.firstElement, desc.numElements, desc.structureByteStride);
            if (!result)
            {
                return Result::fail(
                    Code::CreateFailed,
                    Severity::Error,
                    "Failed to create UAV for the resource.");
            }
            ids.emplace_back(tableId);
            break;
        }
        case ViewType::UnorderedAccessRawBuffer:
        {
            // RAW UAV を作成する
            TableID tableId = m_descriptorAllocator.allocate(convert_view_kind(desc.type));
            Result result = m_descriptorAllocator.create_uav_raw_buffer(tableId, &resource, desc.firstElement, desc.numElements);
            if (!result)
            {
                return Result::fail(
                    Code::CreateFailed,
                    Severity::Error,
                    "Failed to create RAW UAV for the resource.");
            }
            ids.emplace_back(tableId);
            break;
        }
        case ViewType::ShaderResourceTexture2D:
        {
            // SRV を作成する
            TableID tableId = m_descriptorAllocator.allocate(convert_view_kind(desc.type));
            Result result = m_descriptorAllocator.create_srv_texture_2d(tableId, &resource, convert_color_format(desc.colorFormat), desc.mipSlice, desc.mipLevels);
            if (!result)
            {
                return Result::fail(
                    Code::CreateFailed,
                    Severity::Error,
                    "Failed to create SRV for the texture resource.");
            }
            ids.emplace_back(tableId);
            break;
        }
        case ViewType::UnorderedAccessTexture2D:
        {
            // UAV を作成する
            TableID tableId = m_descriptorAllocator.allocate(convert_view_kind(desc.type));
            Result result = m_descriptorAllocator.create_uav_texture_2d(tableId, &resource, convert_color_format(desc.colorFormat), desc.mipSlice);
            if (!result)
            {
                return Result::fail(
                    Code::CreateFailed,
                    Severity::Error,
                    "Failed to create UAV for the texture resource.");
            }
            ids.emplace_back(tableId);
            break;
        }
        case ViewType::RenderTarget:
        {
            // RTV を作成する
            TableID tableId = m_descriptorAllocator.allocate(convert_view_kind(desc.type));
            Result result = m_descriptorAllocator.create_rtv(tableId, &resource, convert_color_format(desc.colorFormat), desc.mipSlice);
            if (!result)
            {
                return Result::fail(
                    Code::CreateFailed,
                    Severity::Error,
                    "Failed to create RTV for the texture resource.");
            }
            ids.emplace_back(tableId);
            break;
        }
        case ViewType::DepthStencil:
        {
            // DSV を作成する
            TableID tableId = m_descriptorAllocator.allocate(convert_view_kind(desc.type));
            Result result = m_descriptorAllocator.create_dsv(tableId, &resource, convert_color_format(desc.colorFormat), desc.mipSlice);
            if (!result)
            {
                return Result::fail(
                    Code::CreateFailed,
                    Severity::Error,
                    "Failed to create DSV for the texture resource.");
            }
            ids.emplace_back(tableId);
            break;
        }
        default:
            break;
        }
        return Result::ok();
    }
    Result DX12ViewManager::get_view(std::string_view name, ViewHandle& out)
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
                "View with the given name was not found.");
        }
    }
}
