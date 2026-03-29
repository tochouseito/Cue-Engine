#include "DX12TextureManager.h"

namespace Cue::RHI::DX12
{
    namespace
    {
        DXGI_FORMAT get_texture_dxgi_format(const TextureDesc& desc)
        {
            if (desc.type == TextureType::DepthStencil)
            {
                return convert_dsv_format(desc.dsvFormat);
            }

            return convert_color_format(desc.colorFormat);
        }

        D3D12_RESOURCE_FLAGS get_texture_resource_flags(const TextureDesc& desc)
        {
            D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
            if (desc.type == TextureType::RenderTarget)
            {
                flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            }
            if (desc.type == TextureType::DepthStencil)
            {
                flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            }
            if (desc.allowUnorderedAccess)
            {
                flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            }
            return flags;
        }
    }

    Result DX12TextureManager::create_texture(const TextureDesc& desc, TextureHandle& out)
    {
        // 1) 記述を検証して、FrameGraph が要求する transient/imported の前提を崩さないようにします。
        if (desc.defaultHeapCount == 0)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Texture must have at least one default heap resource.");
        }
        if (desc.uploadHeapCount != 0)
        {
            return Result::fail(
                    Code::Unsupported,
                Severity::Error,
                "Texture upload heap creation is not supported by DX12TextureManager.");
        }
        if (desc.width == 0 || desc.height == 0 || desc.depthOrArraySize == 0 || desc.mipLevels == 0)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Texture dimensions must be greater than 0.");
        }

        // 2) D3D12 のリソース記述を作って、要求された buffering 数ぶん実体を確保します。
        DX12TextureRecord record{};
        record.desc = desc;

        D3D12_HEAP_PROPERTIES heapProperties{};
        heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC resourceDesc{};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resourceDesc.Alignment = 0;
        resourceDesc.Width = desc.width;
        resourceDesc.Height = desc.height;
        resourceDesc.DepthOrArraySize = static_cast<UINT16>(desc.depthOrArraySize);
        resourceDesc.MipLevels = static_cast<UINT16>(desc.mipLevels);
        resourceDesc.Format = get_texture_dxgi_format(desc);
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.SampleDesc.Quality = 0;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        resourceDesc.Flags = get_texture_resource_flags(desc);

        D3D12_CLEAR_VALUE clearValue{};
        const D3D12_CLEAR_VALUE* clearValuePtr = nullptr;
        if (desc.type == TextureType::RenderTarget)
        {
            clearValue.Format = convert_color_format(desc.colorFormat);
            clearValue.Color[0] = 0.0f;
            clearValue.Color[1] = 0.0f;
            clearValue.Color[2] = 0.0f;
            clearValue.Color[3] = 1.0f;
            clearValuePtr = &clearValue;
        }
        else if (desc.type == TextureType::DepthStencil)
        {
            clearValue.Format = convert_dsv_format(desc.dsvFormat);
            clearValue.DepthStencil.Depth = 1.0f;
            clearValue.DepthStencil.Stencil = 0;
            clearValuePtr = &clearValue;
        }

        std::wstring wideName{};
        PAL::Win::utf8_to_wide(desc.name, &wideName);
        for (uint32_t i = 0; i < desc.defaultHeapCount; ++i)
        {
            DX12GpuResource resource{};
            Result result = resource.create(
                *m_renderDevice.get_d3d12_device(),
                heapProperties,
                D3D12_HEAP_FLAG_NONE,
                resourceDesc,
                convert_resource_state(desc.initialState),
                clearValuePtr,
                wideName);
            if (!result)
            {
                return result;
            }

            record.defaultResources.emplace_back(std::move(resource));
        }

        // 3) registry と名前表へ登録して、FrameGraph compile から引けるようにします。
        TextureHandle handle = m_textureRegistry.create(record);
        if (!desc.name.empty())
        {
            m_nameToHandlesMap[Core::fnv1a64(desc.name)] = handle;
        }

        out = handle;
        return Result::ok();
    }

    Result DX12TextureManager::destroy_texture(TextureHandle handle)
    {
        // 1) ハンドルを解決して、まだ GPU 利用中の実体を破棄しないように止めます。
        Result result = Result::ok();
        const bool found = m_textureRegistry.with(handle, [this, handle, &result](DX12TextureRecord& record)
            {
                for (const DX12GpuResource& resource : record.defaultResources)
                {
                    if (resource.is_in_use())
                    {
                        result = Result::fail(
                            Code::AccessDenied,
                            Severity::Error,
                            "Failed to destroy texture because one or more resources are still in use.");
                        return;
                    }
                }

                if (!record.desc.name.empty())
                {
                    const Core::ResourceNameId nameId = Core::fnv1a64(record.desc.name);
                    const auto it = m_nameToHandlesMap.find(nameId);
                    if (it != m_nameToHandlesMap.end() && it->second == handle)
                    {
                        m_nameToHandlesMap.erase(it);
                    }
                }

                for (DX12GpuResource& resource : record.defaultResources)
                {
                    if (!resource.destroy())
                    {
                        result = Result::fail(
                            Code::AccessDenied,
                            Severity::Error,
                            "Failed to destroy texture because one or more resources are still in use.");
                        return;
                    }
                }
            });

        if (!found)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "Texture handle was not found.");
        }
        if (!result)
        {
            return result;
        }
        if (!m_textureRegistry.destroy(handle))
        {
            return Result::fail(
                Code::InternalError,
                Severity::Error,
                "Failed to remove texture record from registry.");
        }

        return Result::ok();
    }

    Result DX12TextureManager::import_texture(
        const TextureDesc& desc,
        const std::vector<ComPtr<ID3D12Resource>>& defaultResources,
        D3D12_RESOURCE_STATES initialState,
        TextureHandle& out)
    {
        // 1) 外部所有の resource 群を wrapper 化して、FrameGraph から通常の texture と同じ経路で使えるようにします。
        if (defaultResources.empty())
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Imported texture must have at least one default resource.");
        }

        DX12TextureRecord record{};
        record.desc = desc;
        record.imported = true;

        std::wstring wideName{};
        PAL::Win::utf8_to_wide(desc.name, &wideName);
        for (const ComPtr<ID3D12Resource>& sourceResource : defaultResources)
        {
            if (!sourceResource)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Imported texture contains a null D3D12 resource.");
            }

            DX12GpuResource resource{};
            Result result = resource.adopt_existing(ComPtr<ID3D12Resource>(sourceResource), initialState, wideName);
            if (!result)
            {
                return result;
            }

            record.defaultResources.emplace_back(std::move(resource));
        }

        // 2) 通常 texture と同じく registry へ登録して、frame-index 解決を manager に任せます。
        TextureHandle handle = m_textureRegistry.create(record);
        if (!desc.name.empty())
        {
            m_nameToHandlesMap[Core::fnv1a64(desc.name)] = handle;
        }

        out = handle;
        return Result::ok();
    }

    bool DX12TextureManager::try_get_record(TextureHandle handle, DX12TextureRecord*& outRecord)
    {
        // 1) backend 内部だけが実体へ触るので、生ポインタを短期参照として返します。
        outRecord = m_textureRegistry.get(handle);
        return outRecord != nullptr;
    }

    bool DX12TextureManager::try_get_record(TextureHandle handle, const DX12TextureRecord*& outRecord) const
    {
        // 1) const 経路も同じく registry の生存検証を通して返します。
        outRecord = m_textureRegistry.get(handle);
        return outRecord != nullptr;
    }
}
