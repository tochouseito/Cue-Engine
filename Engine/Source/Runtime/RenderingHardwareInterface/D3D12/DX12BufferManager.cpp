#include "DX12BufferManager.h"

namespace Cue::RHI::DX12
{
    namespace
    {
        [[nodiscard]] D3D12_RESOURCE_FLAGS get_default_buffer_resource_flags(const BufferDesc& desc) noexcept
        {
            // UAV と raw buffer は resource 作成時点で unordered access 許可が必要になる。
            switch (desc.type)
            {
            case BufferType::UnorderedAccess:
            case BufferType::Raw:
                return D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            default:
                return D3D12_RESOURCE_FLAG_NONE;
            }
        }
    }

    Result DX12BufferManager::create_buffer(const BufferDesc& desc, BufferHandle& out)
    {
        // 1 つの論理 buffer に対して、default/upload/readback の実 resource を必要数だけ作る。
        // Frame ring 用の複数 resource は同じ BufferHandle の record 内にまとめて保持する。
        DX12BufferRecord record{};

        // --- 引数の検査 ---
        CUE_ASSERT_MSG(desc.defaultHeapCount + desc.uploadHeapCount + desc.readbackHeapCount > 0, "Buffer must have at least one heap.");
        CUE_ASSERT_MSG(desc.size > 0, "Buffer size must be greater than 0.");
        CUE_ASSERT_MSG(desc.stride > 0, "Buffer stride must be greater than 0.");
        CUE_ASSERT_MSG(desc.elementCount > 0, "Buffer element count must be greater than 0.");
        CUE_ASSERT_MSG(desc.alignment > 0, "Buffer alignment must be greater than 0.");
        CUE_ASSERT_MSG(desc.type != BufferType::Unknown, "Buffer type must be specified.");

        // デフォルトヒープバッファの作成
        for (uint32_t i = 0; i < desc.defaultHeapCount; ++i)
        {
            // バッファの初期化処理
            DX12GpuResource resource;
            const D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON; // デフォルトヒープは初期状態を COMMON にすることが多いが、必要に応じて変更する
            D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE;
            D3D12_HEAP_PROPERTIES heapProperties = {};
            heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT; // デフォルトヒープは GPU 専用のヒープタイプ
            D3D12_RESOURCE_DESC resourceDesc = {};
            resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            resourceDesc.Width = desc.type == BufferType::Constant ? Math::round_up_to_multiple(desc.size, 256) : desc.size; // 定数バッファはサイズを 256 バイト境界に揃える
            resourceDesc.Height = 1;
            resourceDesc.DepthOrArraySize = 1;
            resourceDesc.MipLevels = 1;
            resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
            resourceDesc.SampleDesc = { 1, 0 };
            resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            resourceDesc.Alignment = 0;
            resourceDesc.Flags = get_default_buffer_resource_flags(desc);
            // リソース名の変換
            std::wstring name = L"";
            PAL::Win::utf8_to_wide(desc.name, &name);
            // 実リソース生成
            resource.create(
                *m_renderDevice.get_d3d12_device(),
                heapProperties,
                heapFlags,
                resourceDesc,
                initialState,
                nullptr,
                name);
            // 成功したらレコードに追加
            record.defaultResources.emplace_back(std::move(resource));
        }

        // アップロードヒープバッファの作成
        for (uint32_t i = 0; i < desc.uploadHeapCount; ++i)
        {
            // バッファの初期化処理
            DX12GpuResource resource;
            D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE;
            D3D12_HEAP_PROPERTIES heapProperties = {};
            heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD; // アップロードヒープは CPU アクセス可能なヒープタイプ
            D3D12_RESOURCE_DESC resourceDesc = {};
            resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            resourceDesc.Width = desc.type == BufferType::Constant ? Math::round_up_to_multiple(desc.size, 256) : desc.size; // 定数バッファはサイズを 256 バイト境界に揃える
            resourceDesc.Height = 1;
            resourceDesc.DepthOrArraySize = 1;
            resourceDesc.MipLevels = 1;
            resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
            resourceDesc.SampleDesc = { 1, 0 };
            resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            resourceDesc.Alignment = 0;
            resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
            // リソース名の変換
            std::wstring name = L"";
            PAL::Win::utf8_to_wide(desc.name, &name);
            // 実リソース生成
            resource.create(
                *m_renderDevice.get_d3d12_device(),
                heapProperties,
                heapFlags,
                resourceDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ, // アップロードヒープは常に GENERIC_READ で作成
                nullptr,
                name);
            Result mapResult = resource.map_persistent();
            if (!mapResult)
            {
                return mapResult;
            }
            // 成功したらレコードに追加
            record.uploadResources.emplace_back(std::move(resource));
        }

        // 読み戻しヒープバッファの作成
        for (uint32_t i = 0; i < desc.readbackHeapCount; ++i)
        {
            DX12GpuResource resource;
            D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE;
            D3D12_HEAP_PROPERTIES heapProperties = {};
            heapProperties.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC resourceDesc = {};
            resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            resourceDesc.Width = desc.size;
            resourceDesc.Height = 1;
            resourceDesc.DepthOrArraySize = 1;
            resourceDesc.MipLevels = 1;
            resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
            resourceDesc.SampleDesc = { 1, 0 };
            resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            resourceDesc.Alignment = 0;
            resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

            std::wstring name = L"";
            PAL::Win::utf8_to_wide(desc.name, &name);
            Result createResult = resource.create(
                *m_renderDevice.get_d3d12_device(),
                heapProperties,
                heapFlags,
                resourceDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                name);
            if (!createResult)
            {
                return createResult;
            }
            Result mapResult = resource.map_persistent();
            if (!mapResult)
            {
                return mapResult;
            }

            record.readbackResources.emplace_back(std::move(resource));
        }

        // レコードの保存
        record.desc = desc;
        BufferHandle handle = m_bufferRegistry.create(record);
        if (!desc.name.empty())
        {
            m_nameToHandlesMap[Core::fnv1a64(desc.name)] = handle;
        }

        out = std::move(handle);

        return Result::ok();
    }

    Result DX12BufferManager::get_buffer(std::string_view name, BufferHandle& out)
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
                "Buffer with the given name was not found.");
        }
    }

    Result DX12BufferManager::get_upload_buffer_view(BufferHandle handle, UploadBufferView& outView)
    {
        // CPU 書き込み用の view は upload heap を永続 map して返す。
        // 呼び出し側は frame index ごとの slice を SlotUploader 経由で書き込む。
        // - ハンドルを解決して、upload heap 群と記述情報を同じ世代の record から読む
        DX12BufferRecord* record = nullptr;
        outView = {};
        if (!try_get_record(handle, &record) || record == nullptr)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Buffer record not found for the given handle.");
        }

        // - SlotUploader の初期化に必要な容量・アラインメント・stride を検証する
        if (record->desc.elementCount == 0 || record->desc.alignment == 0 || record->desc.stride == 0)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Buffer description is not valid for slot uploaders.");
        }
        if (record->uploadResources.empty())
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Upload heap resources were not created for the given buffer.");
        }

        const uint64_t alignedStride = Math::round_up_to_multiple(
            static_cast<uint64_t>(record->desc.stride),
            static_cast<uint64_t>(record->desc.alignment));
        const uint64_t requiredBytes = alignedStride * static_cast<uint64_t>(record->desc.elementCount);

        // Upload heap view は map 済み領域に収まる範囲だけを公開する
        outView.alignment = record->desc.alignment;
        outView.stride = record->desc.stride;
        outView.elementCount = record->desc.elementCount;
        outView.mappedDatas.reserve(record->uploadResources.size());
        for (DX12GpuResource& resource : record->uploadResources)
        {
            if (resource.mapped_data() == nullptr)
            {
                outView = {};
                return Result::fail(
                    Code::InternalError,
                    Severity::Error,
                    "Upload heap resource is not mapped.");
            }
            if (resource.get_buffer_size() < requiredBytes)
            {
                outView = {};
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Upload heap resource is smaller than the requested slot upload range.");
            }

            outView.mappedDatas.emplace_back(resource.mapped_data());
        }

        return Result::ok();
    }

    Result DX12BufferManager::get_readback_buffer_view(
        BufferHandle handle,
        ReadbackBufferView& outView)
    {
        DX12BufferRecord* record = nullptr;
        outView = {};
        if (!try_get_record(handle, &record) || record == nullptr)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Buffer record not found for the given handle.");
        }
        if (record->desc.elementCount == 0 || record->desc.alignment == 0 ||
            record->desc.stride == 0)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Buffer description is not valid for readback.");
        }
        if (record->readbackResources.empty())
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Readback heap resources were not created for the given buffer.");
        }

        outView.alignment = record->desc.alignment;
        outView.stride = record->desc.stride;
        outView.elementCount = record->desc.elementCount;
        outView.mappedDatas.reserve(record->readbackResources.size());
        for (DX12GpuResource& resource : record->readbackResources)
        {
            if (resource.mapped_data() == nullptr)
            {
                outView = {};
                return Result::fail(
                    Code::InternalError,
                    Severity::Error,
                    "Readback heap resource is not mapped.");
            }

            outView.mappedDatas.emplace_back(resource.mapped_data());
        }

        return Result::ok();
    }

    Result DX12BufferManager::destroy_buffer(BufferHandle handle)
    {
        // GPU 使用中の resource は即時破棄せず失敗として返す。
        // 上位側は fence 完了後に再試行することで use-after-free を避ける。
        // ハンドルの解決と、破棄前に全リソースが解放可能かを確認する
        Result result = Result::ok();

        const bool found = m_bufferRegistry.with(handle, [this, handle, &result](DX12BufferRecord&
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

                for (const DX12GpuResource& resource : record.uploadResources)
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

                for (const DX12GpuResource& resource : record.readbackResources)
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

                for (DX12GpuResource& resource : record.uploadResources)
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

                for (DX12GpuResource& resource : record.readbackResources)
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
        if (!m_bufferRegistry.destroy(handle))
        {
            return Result::fail(
                Code::InternalError,
                Severity::Error,
                "Failed to remove buffer record from registry.");
        }

        return Result::ok();
    }

    bool DX12BufferManager::try_get_record(BufferHandle handle, DX12BufferRecord** outRecord)
    {
        // ハンドルの解決とレコードの取得
        *outRecord = nullptr;
        *outRecord = m_bufferRegistry.ref_get(handle);
        return *outRecord != nullptr;
    }
}
