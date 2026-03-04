#include "DX12ViewManager.h"

namespace Cue::GraphicsCore::DX12
{
    namespace
    {
        [[nodiscard]] size_t hash_combine(size_t seed, size_t value) noexcept
        {
            return seed ^ (value + 0x9e3779b9u + (seed << 6) + (seed >> 2));
        }

        [[nodiscard]] DescriptorAllocator::TableKind table_kind_for_buffer_view(ViewType type) noexcept
        {
            switch (type)
            {
            case ViewType::ConstantBuffer:
            case ViewType::ShaderResource:
            case ViewType::UnorderedAccess:
            default:
                return DescriptorAllocator::TableKind::Buffers;
            }
        }
    }

    size_t DX12ViewManager::BufferViewKeyHash::operator()(const BufferViewKey& key) const noexcept
    {
        size_t seed = 0;
        seed = hash_combine(seed, std::hash<uint32_t>{}(key.handle.index));
        seed = hash_combine(seed, std::hash<uint32_t>{}(key.handle.generation));
        seed = hash_combine(seed, std::hash<uint32_t>{}(static_cast<uint32_t>(key.desc.type)));
        seed = hash_combine(seed, std::hash<uint64_t>{}(key.desc.firstElement));
        seed = hash_combine(seed, std::hash<uint32_t>{}(key.desc.numElements));
        seed = hash_combine(seed, std::hash<uint32_t>{}(key.desc.structureByteStride));
        seed = hash_combine(seed, std::hash<uint64_t>{}(key.desc.byteOffset));
        seed = hash_combine(seed, std::hash<uint32_t>{}(key.desc.byteSize));
        seed = hash_combine(seed, std::hash<bool>{}(key.desc.isRaw));
        return seed;
    }

    size_t DX12ViewManager::TextureViewKeyHash::operator()(const TextureViewKey& key) const noexcept
    {
        size_t seed = 0;
        seed = hash_combine(seed, std::hash<uint32_t>{}(key.handle.index));
        seed = hash_combine(seed, std::hash<uint32_t>{}(key.handle.generation));
        seed = hash_combine(seed, std::hash<uint32_t>{}(static_cast<uint32_t>(key.desc.type)));
        seed = hash_combine(seed, std::hash<uint32_t>{}(key.desc.mipSlice));
        seed = hash_combine(seed, std::hash<uint32_t>{}(key.desc.mipLevels));
        return seed;
    }

    Result DX12ViewManager::get_buffer_view(BufferHandle handle, const BufferViewDesc& desc, ViewHandle& outHandle)
    {
        // 1) 同一 handle + desc の view は再利用して、descriptor heap 消費と再作成コストを抑える。
        const BufferViewKey key{ handle, desc };
        const auto found = m_bufferViewCache.find(key);
        if (found != m_bufferViewCache.end())
        {
            if (m_viewRegistry.with(found->second, [&outHandle, cached = found->second](const ViewRecord&)
            {
                outHandle = cached;
            }))
            {
                return Result::ok();
            }
            m_bufferViewCache.erase(found);
        }

        // 2) キャッシュに無い場合だけ新規 descriptor を作って view handle を返す。
        return create_buffer_view(handle, desc, outHandle);
    }

    Result DX12ViewManager::get_texture_view(TextureHandle handle, const TextureViewDesc& desc, ViewHandle& outHandle)
    {
        // 1) texture view も desc 単位でキャッシュし、SRV/RTV の重複生成を避ける。
        const TextureViewKey key{ handle, desc };
        const auto found = m_textureViewCache.find(key);
        if (found != m_textureViewCache.end())
        {
            if (m_viewRegistry.with(found->second, [&outHandle, cached = found->second](const ViewRecord&)
            {
                outHandle = cached;
            }))
            {
                return Result::ok();
            }
            m_textureViewCache.erase(found);
        }

        // 2) キャッシュに無い場合だけ descriptor を作って view handle を返す。
        return create_texture_view(handle, desc, outHandle);
    }

    Result DX12ViewManager::destroy_view(ViewHandle handle)
    {
        // 1) registry に保持した key と descriptor slot を回収し、キャッシュと実体を一緒に解放する。
        ViewRecord record{};
        if (!m_viewRegistry.try_get(handle, record))
        {
            return Result::fail(Facility::Graphics, Code::NotFound, Severity::Warning, 0, "View handle is not alive");
        }

        if (record.resourceKind == ResourceKind::Buffer)
        {
            m_bufferViewCache.erase(BufferViewKey{ record.bufferHandle, record.bufferDesc });
        }
        else
        {
            m_textureViewCache.erase(TextureViewKey{ record.textureHandle, record.textureDesc });
        }

        m_descriptorAllocator.free_table(record.tableId);
        if (!m_viewRegistry.destroy(handle))
        {
            return Result::fail(Facility::Graphics, Code::InvalidState, Severity::Error, 0, "Failed to destroy view handle");
        }
        return Result::ok();
    }

    Result DX12ViewManager::get_descriptor_handle(ViewHandle handle, DescriptorHandle& outHandle) const
    {
        // 1) view type に応じて CPU 専用 heap と shader-visible heap の返し方を切り替える。
        bool found = false;
        found = m_viewRegistry.with(handle, [this, &outHandle, &found](const ViewRecord& record)
        {
            found = true;
            if (record.viewType == ViewType::RenderTarget || record.viewType == ViewType::DepthStencil)
            {
                const D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_descriptorAllocator.get_cpu_handle(record.tableId);
                outHandle.cpuPtr = cpuHandle.ptr;
                outHandle.gpuPtr = 0;
                outHandle.shaderVisible = false;
                return;
            }

            const D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_descriptorAllocator.get_cpu_handle_gpu_visible(record.tableId);
            const D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = m_descriptorAllocator.get_gpu_handle(record.tableId);
            outHandle.cpuPtr = cpuHandle.ptr;
            outHandle.gpuPtr = gpuHandle.ptr;
            outHandle.shaderVisible = true;
        });
        if (!found)
        {
            return Result::fail(Facility::Graphics, Code::NotFound, Severity::Warning, 0, "View handle is not alive");
        }
        return Result::ok();
    }

    Result DX12ViewManager::create_buffer_view(BufferHandle handle, const BufferViewDesc& desc, ViewHandle& outHandle)
    {
        // 1) backend resource を解決し、view type ごとに適切な descriptor builder を選ぶ。
        GpuBufferResource* buffer = nullptr;
        const Result resolveResult = m_bufferManager.try_get_buffer(handle, buffer);
        if (!resolveResult)
        {
            return resolveResult;
        }

        DescriptorAllocator::TableID tableId = m_descriptorAllocator.allocate(table_kind_for_buffer_view(desc.type));
        if (!tableId.valid())
        {
            return Result::fail(Facility::Graphics, Code::CreationFailed, Severity::Error, 0, "Failed to allocate descriptor slot for buffer view.");
        }

        Result createResult = Result::ok();
        switch (desc.type)
        {
        case ViewType::ConstantBuffer:
            createResult = m_descriptorAllocator.create_cbv(tableId, buffer, desc.byteOffset, desc.byteSize);
            break;
        case ViewType::ShaderResource:
            createResult = desc.isRaw
                ? m_descriptorAllocator.create_srv_raw_buffer(tableId, buffer, desc.firstElement, desc.numElements)
                : m_descriptorAllocator.create_srv_buffer(tableId, buffer, desc.firstElement, desc.numElements, desc.structureByteStride);
            break;
        case ViewType::UnorderedAccess:
            createResult = desc.isRaw
                ? m_descriptorAllocator.create_uav_raw_buffer(tableId, buffer, desc.firstElement, desc.numElements)
                : m_descriptorAllocator.create_uav_buffer(tableId, buffer, desc.firstElement, desc.numElements, desc.structureByteStride);
            break;
        default:
            createResult = Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Error, 0, "Buffer view type is not supported.");
            break;
        }
        if (!createResult)
        {
            m_descriptorAllocator.free_table(tableId);
            return createResult;
        }

        // 2) view handle へ key と descriptor slot を保持し、次回はキャッシュから再利用する。
        ViewRecord record{};
        record.resourceKind = ResourceKind::Buffer;
        record.viewType = desc.type;
        record.bufferHandle = handle;
        record.bufferDesc = desc;
        record.tableId = tableId;
        outHandle = m_viewRegistry.create(record);
        m_bufferViewCache.emplace(BufferViewKey{ handle, desc }, outHandle);
        return Result::ok();
    }

    Result DX12ViewManager::create_texture_view(TextureHandle handle, const TextureViewDesc& desc, ViewHandle& outHandle)
    {
        // 1) texture 実体を解決し、view type ごとに SRV/UAV/RTV/DSV を作り分ける。
        GpuTextureResource* texture = nullptr;
        const Result resolveResult = m_textureManager.try_get_texture(handle, texture);
        if (!resolveResult)
        {
            return resolveResult;
        }

        DescriptorAllocator::TableKind tableKind = DescriptorAllocator::TableKind::Textures;
        if (desc.type == ViewType::RenderTarget)
        {
            tableKind = DescriptorAllocator::TableKind::RenderTargets;
        }
        else if (desc.type == ViewType::DepthStencil)
        {
            tableKind = DescriptorAllocator::TableKind::DepthStencils;
        }

        DescriptorAllocator::TableID tableId = m_descriptorAllocator.allocate(tableKind);
        if (!tableId.valid())
        {
            return Result::fail(Facility::Graphics, Code::CreationFailed, Severity::Error, 0, "Failed to allocate descriptor slot for texture view.");
        }

        const DXGI_FORMAT format = texture->get_resource_desc().Format;
        Result createResult = Result::ok();
        switch (desc.type)
        {
        case ViewType::ShaderResource:
            createResult = m_descriptorAllocator.create_srv_texture_2d(tableId, texture, format, desc.mipSlice, desc.mipLevels);
            break;
        case ViewType::UnorderedAccess:
            createResult = m_descriptorAllocator.create_uav_texture_2d(tableId, texture, format, desc.mipSlice);
            break;
        case ViewType::RenderTarget:
            createResult = m_descriptorAllocator.create_rtv(tableId, texture, format, desc.mipSlice);
            break;
        case ViewType::DepthStencil:
            createResult = m_descriptorAllocator.create_dsv(tableId, texture, format, desc.mipSlice);
            break;
        default:
            createResult = Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Error, 0, "Texture view type is not supported.");
            break;
        }
        if (!createResult)
        {
            m_descriptorAllocator.free_table(tableId);
            return createResult;
        }

        // 2) texture view も cache key と紐付けて保持し、同一 mip/view type を再利用する。
        ViewRecord record{};
        record.resourceKind = ResourceKind::Texture;
        record.viewType = desc.type;
        record.textureHandle = handle;
        record.textureDesc = desc;
        record.tableId = tableId;
        outHandle = m_viewRegistry.create(record);
        m_textureViewCache.emplace(TextureViewKey{ handle, desc }, outHandle);
        return Result::ok();
    }
} // namespace Cue::GraphicsCore::DX12
