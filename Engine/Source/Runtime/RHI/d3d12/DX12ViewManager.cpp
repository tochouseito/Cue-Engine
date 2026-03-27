#include "DX12ViewManager.h"

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
                return TableKind::Buffers; // デフォルトはバッファ用テーブルとする
            }
        }
    }

    Result DX12ViewManager::create_view(const ViewDesc& desc, ViewHandle& out)
    {
        DX12ViewRecord record{};
        record.desc = desc;

        // View の作成
        Result result;
        switch (desc.type)
        {
        case ViewType::ConstantBuffer:
        {
            // Buffer の実体を取得する
            DX12BufferRecord* bufferRecord = nullptr;
            if (!m_bufferManager.try_get_record(desc.bufferHandle, bufferRecord))
            {
                return Result::fail(
                    Code::NotFound,
                    Severity::Error,
                    "Buffer not found for the given handle.");
            }

            // テーブルスロットを割り当てて CBV を作成する
            // Default Resource
            if (!bufferRecord)
            {
                return Result::fail(
                    Code::NotFound,
                    Severity::Error,
                    "Buffer record not found for the given handle.");
            }
            for (auto& resource : bufferRecord->defaultResources)
            {
                TableID tableId = m_descriptorAllocator.allocate(convert_buffer_kind(desc.bufferKind));
                result = m_descriptorAllocator.create_cbv(tableId, &resource, desc.byteOffset, desc.byteSize);
                if (!result)
                {
                    return Result::fail(
                        Code::CreateFailed,
                        Severity::Error,
                        "Failed to create CBV for default resource.");
                }
                record.uploadTableIds.emplace_back(tableId);
            }
            // Upload Resource
            for (auto& resource : bufferRecord->uploadResources)
            {
                TableID tableId = m_descriptorAllocator.allocate(convert_buffer_kind(desc.bufferKind));
                result = m_descriptorAllocator.create_cbv(tableId, &resource, desc.byteOffset, desc.byteSize);
                if (!result)
                {
                    return Result::fail(
                        Code::CreateFailed,
                        Severity::Error,
                        "Failed to create CBV for upload resource.");
                }
                record.uploadTableIds.emplace_back(tableId);
            }
            break;
        }
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
}
