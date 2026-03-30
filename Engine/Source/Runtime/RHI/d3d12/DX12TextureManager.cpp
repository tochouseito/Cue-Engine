#include "DX12TextureManager.h"

namespace Cue::RHI::DX12
{
    Result DX12TextureManager::create_texture(const TextureDesc& desc, TextureHandle& out)
    {
        DX12TextureRecord record{};

        // --- 引数の検査 ---
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

        // デフォルトヒープバッファの作成
        for (uint32_t i = 0; i < desc.bufferCount; ++i)
        {
            // バッファの初期化処理
            DX12GpuResource resource;
            D3D12_RESOURCE_DESC resourceDesc = {};
            resourceDesc.Width = desc.width;
            resourceDesc.Height = desc.height;
            resourceDesc.MipLevels = desc.mipLevels;
            resourceDesc.DepthOrArraySize = desc.arraySize;
            resourceDesc.SampleDesc.Count = desc.sampleCount;
            resourceDesc.Format = convert_color_format(desc.format);
            resourceDesc.Dimension = desc.type == TextureType::Texture2D ? D3D12_RESOURCE_DIMENSION_TEXTURE2D : D3D12_RESOURCE_DIMENSION_TEXTURE3D;
            resourceDesc.Flags = desc.kind == TextureKind::RenderTarget ? D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET :
                                 desc.kind == TextureKind::DepthStencil ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL :
                                 D3D12_RESOURCE_FLAG_NONE;
            // クリア値の設定
            D3D12_CLEAR_VALUE clearValue = {};
            clearValue.Format = convert_color_format(desc.format);
            clearValue.Color[0] = desc.clearColor[0];
            clearValue.Color[1] = desc.clearColor[1];
            clearValue.Color[2] = desc.clearColor[2];
            clearValue.Color[3] = desc.clearColor[3];

            // ヒーププロパティの設定
            D3D12_HEAP_PROPERTIES heapProperties = {};
            heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT; // デフォルトヒープは GPU 専用のヒープタイプ
            D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE;
            
            // リソース名の変換
            std::wstring name = L"";
            PAL::Win::utf8_to_wide(desc.name, &name);
            // 実リソース生成
            resource.create(
                *m_renderDevice.get_d3d12_device(),
                heapProperties,
                heapFlags,
                resourceDesc,
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                name);
            // 成功したらレコードに追加
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

    bool DX12TextureManager::try_get_record(TextureHandle handle, DX12TextureRecord** outRecord)
    {
        // ハンドルの解決とレコードの取得
        *outRecord = nullptr;
        *outRecord = m_textureRegistry.get(handle);
        return *outRecord != nullptr;
    }

    Result DX12TextureManager::register_external_texture(DX12TextureRecord& record, TextureHandle& out)
    {
        // レコードの保存
        TextureHandle handle = m_textureRegistry.create(record);
        if (!record.desc.name.empty())
        {
            m_nameToHandlesMap[Core::fnv1a64(record.desc.name)] = handle;
        }

        out = std::move(handle);

        return Result::ok();
    }
}
