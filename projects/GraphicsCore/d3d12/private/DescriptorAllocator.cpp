#include "DescriptorAllocator.h"

namespace Cue::GraphicsCore::DX12
{
    Result DescriptorAllocator::initialize(uint32_t texCap, uint32_t bufCap, uint32_t rtCap, uint32_t dsCap)
    {
        // 1) ヒープ種別ごとにディスクリプタヒープとテーブルを初期化する
        HRESULT hr = S_OK;
        ID3D12Device* device = m_renderDevice.get_d3d12_device();

        for (size_t i = 0; i < static_cast<size_t>(HeapType::kCount); ++i)
        {
            D3D12_DESCRIPTOR_HEAP_TYPE heapType{};
            D3D12_DESCRIPTOR_HEAP_DESC desc{};
            switch (static_cast<HeapType>(i))
            {
            case HeapType::CBV_SRV_UAV:
            {
                // CPU 可視ヒープ
                heapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                m_descriptorSizes[i] = device->GetDescriptorHandleIncrementSize(heapType);

                desc.Type = heapType;
                desc.NumDescriptors = texCap + bufCap;
                desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
                hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_descriptorHeaps[i]));
                if (FAILED(hr))
                {
                    Assert::cue_assert(false, "DescriptorAllocator", "Failed CreateDescriptorHeap. HRESULT: {:#X}", static_cast<uint32_t>(hr));
                }
                SetD3D12Name(m_descriptorHeaps[i].Get(), L"DescriptorHeap");

                // GPU 可視ヒープ
                desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_gpuSrvUavHeap));
                if (FAILED(hr))
                {
                    Assert::cue_assert(false, "DescriptorAllocator", "Failed CreateDescriptorHeap. HRESULT: {:#X}", static_cast<uint32_t>(hr));
                }
                SetD3D12Name(m_gpuSrvUavHeap.Get(), L"GpuCBV_SRV_UAV_Heap");

                // テーブル初期化（Textures / Buffers）
                m_textures.m_heapType = HeapType::CBV_SRV_UAV;
                m_buffers.m_heapType = HeapType::CBV_SRV_UAV;

                m_textures.m_capacity = texCap;
                m_textures.m_baseIndex = 0;
                m_textures.m_freeList.reserve(m_textures.m_capacity);
                for (uint32_t j = 0; j < texCap; ++j)
                {
                    m_textures.m_freeList.push_back(m_textures.m_capacity - 1u - j);
                }

                m_buffers.m_capacity = bufCap;
                m_buffers.m_baseIndex = texCap;
                m_buffers.m_freeList.reserve(m_buffers.m_capacity);
                for (uint32_t j = 0; j < bufCap; ++j)
                {
                    m_buffers.m_freeList.push_back(m_buffers.m_capacity - 1u - j);
                }
            }
            break;

            case HeapType::SAMPLER:
                // サンプラーヒープは未実装
                heapType = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
                break;

            case HeapType::RTV:
            {
                heapType = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
                m_descriptorSizes[i] = device->GetDescriptorHandleIncrementSize(heapType);

                desc.Type = heapType;
                desc.NumDescriptors = rtCap;
                desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
                hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_descriptorHeaps[i]));
                if (FAILED(hr))
                {
                    Assert::cue_assert(false, "DescriptorAllocator", "Failed CreateDescriptorHeap. HRESULT: {:#X}", static_cast<uint32_t>(hr));
                }
                SetD3D12Name(m_descriptorHeaps[i].Get(), L"DescriptorHeap");

                m_renderTargets.m_heapType = HeapType::RTV;
                m_renderTargets.m_capacity = rtCap;
                m_renderTargets.m_freeList.reserve(m_renderTargets.m_capacity);
                for (uint32_t j = 0; j < rtCap; ++j)
                {
                    m_renderTargets.m_freeList.push_back(m_renderTargets.m_capacity - 1u - j);
                }
            }
            break;

            case HeapType::DSV:
            {
                heapType = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
                m_descriptorSizes[i] = device->GetDescriptorHandleIncrementSize(heapType);

                desc.Type = heapType;
                desc.NumDescriptors = dsCap;
                desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
                hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_descriptorHeaps[i]));
                if (FAILED(hr))
                {
                    Assert::cue_assert(false, "DescriptorAllocator", "Failed CreateDescriptorHeap. HRESULT: {:#X}", static_cast<uint32_t>(hr));
                }
                SetD3D12Name(m_descriptorHeaps[i].Get(), L"DescriptorHeap");

                m_depthStencils.m_heapType = HeapType::DSV;
                m_depthStencils.m_capacity = dsCap;
                m_depthStencils.m_freeList.reserve(m_depthStencils.m_capacity);
                for (uint32_t j = 0; j < dsCap; ++j)
                {
                    m_depthStencils.m_freeList.push_back(m_depthStencils.m_capacity - 1u - j);
                }
            }
            break;

            default:
                Assert::cue_assert(false, "DescriptorAllocator", "Unknown HeapType");
                break;
            }
        }

        return Result::ok();
    }
    DescriptorAllocator::TableID DescriptorAllocator::allocate(TableKind k)
    {
        // 1) 対象テーブルを取得して空き状況を確認する
        // 2) 空きがあれば割り当て済みインデックスを返す
        Table& t = get_table(k);
        if (t.m_freeList.empty())
        {
            Assert::cue_assert(false, "DescriptorAllocator", "Descriptor table full, need to expand.");
            ensure_capacity(k, /*needOneMore=*/1);
        }

        if (t.m_freeList.empty())
        {
            // ensure_capacity 未実装経路として致命エラーにする
            return TableID{ k, t.m_generation, TableID::kInvalid };
        }

        uint32_t idx = t.m_freeList.back();
        t.m_freeList.pop_back();
        return TableID{ k, t.m_generation, idx };
    }
    void DescriptorAllocator::free_table(TableID id)
    {
        // 1) 無効 ID を弾いて再利用ミスを防ぐ
        // 2) 空きリストに戻して再割り当て可能にする
        if (!id.valid())
        {
            return;
        }

        Table& t = get_table(id.m_kind);
        t.m_freeList.push_back(id.m_index);
    }
    Result DescriptorAllocator::create_cbv(TableID& id, GpuBufferResource* buffer)
    {
        return create_cbv(id, buffer, 0, 0);
    }
    Result DescriptorAllocator::create_cbv(TableID& id, GpuBufferResource* buffer, uint64_t byteOffset, uint32_t byteSize)
    {
        // 1) 定数バッファ view の範囲を実体サイズへ合わせる。
        if (!id.valid())
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "Invalid TableID.");
        }
        if (buffer == nullptr)
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "Buffer resource is null.");
        }

        const uint64_t bufferSize = buffer->get_byte_size();
        if (byteOffset > bufferSize)
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "CBV byte offset is out of range.");
        }

        const uint64_t resolvedSize = byteSize == 0 ? (bufferSize - byteOffset) : static_cast<uint64_t>(byteSize);
        if (resolvedSize == 0 || byteOffset + resolvedSize > bufferSize)
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "CBV byte range is invalid.");
        }

        D3D12_CONSTANT_BUFFER_VIEW_DESC desc{};
        desc.BufferLocation = buffer->get_resource()->GetGPUVirtualAddress() + byteOffset;
        desc.SizeInBytes = (static_cast<UINT>(resolvedSize) + 255u) & ~255u;

        auto cpuH = get_cpu_handle(id);
        m_renderDevice.get_d3d12_device()->CreateConstantBufferView(&desc, cpuH);
        copy_to_gpu_heap(id);
        return Result::ok();
    }
    Result DescriptorAllocator::create_srv_buffer(TableID& id, GpuBufferResource* buffer)
    {
        return create_srv_buffer(id, buffer, 0, 0, 0);
    }
    Result DescriptorAllocator::create_srv_buffer(TableID& id, GpuBufferResource* buffer, uint64_t firstElement, uint32_t numElements, uint32_t structureByteStride)
    {
        // 1) Structured SRV は element 単位の範囲指定を受け取り、同一 buffer から複数 view を切り出せるようにする。
        if (!id.valid())
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "Invalid TableID.");
        }
        if (buffer == nullptr)
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "Buffer resource is null.");
        }

        const uint32_t stride = structureByteStride == 0 ? buffer->get_stride() : structureByteStride;
        const uint64_t availableElements = buffer->get_num_elements();
        if (firstElement > availableElements)
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "SRV first element is out of range.");
        }
        const uint64_t resolvedNumElements = numElements == 0 ? (availableElements - firstElement) : static_cast<uint64_t>(numElements);
        if (stride == 0 || resolvedNumElements == 0 || firstElement + resolvedNumElements > availableElements)
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "SRV buffer range is invalid.");
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Buffer.FirstElement = firstElement;
        desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        desc.Buffer.NumElements = static_cast<UINT>(resolvedNumElements);
        desc.Buffer.StructureByteStride = stride;

        auto cpuH = get_cpu_handle(id);
        m_renderDevice.get_d3d12_device()->CreateShaderResourceView(buffer->get_resource(), &desc, cpuH);
        copy_to_gpu_heap(id);
        return Result::ok();
    }
    Result DescriptorAllocator::create_srv_raw_buffer(TableID& id, GpuBufferResource* buffer, uint64_t firstElement, uint32_t numElements)
    {
        // 1) Raw SRV は 32-bit 要素として切るので、byte size から有効範囲を導出する。
        if (!id.valid())
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "Invalid TableID.");
        }
        if (buffer == nullptr)
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "Buffer resource is null.");
        }

        const uint64_t availableElements = buffer->get_byte_size() / sizeof(uint32_t);
        if (firstElement > availableElements)
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "Raw SRV first element is out of range.");
        }
        const uint64_t resolvedNumElements = numElements == 0 ? (availableElements - firstElement) : static_cast<uint64_t>(numElements);
        if (resolvedNumElements == 0 || firstElement + resolvedNumElements > availableElements)
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "Raw SRV buffer range is invalid.");
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
        desc.Format = DXGI_FORMAT_R32_TYPELESS;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Buffer.FirstElement = firstElement;
        desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        desc.Buffer.NumElements = static_cast<UINT>(resolvedNumElements);
        desc.Buffer.StructureByteStride = 0;

        auto cpuH = get_cpu_handle(id);
        m_renderDevice.get_d3d12_device()->CreateShaderResourceView(buffer->get_resource(), &desc, cpuH);
        copy_to_gpu_heap(id);
        return Result::ok();
    }
    Result DescriptorAllocator::create_uav_buffer(TableID& id, GpuBufferResource* buffer)
    {
        return create_uav_buffer(id, buffer, 0, 0, 0);
    }
    Result DescriptorAllocator::create_uav_buffer(TableID& id, GpuBufferResource* buffer, uint64_t firstElement, uint32_t numElements, uint32_t structureByteStride)
    {
        // 1) Structured UAV も range 指定対応にして、同一 buffer の別領域へ独立した書き込み view を張れるようにする。
        if (!id.valid())
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "Invalid TableID.");
        }
        if (buffer == nullptr)
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "Buffer resource is null.");
        }

        const uint32_t stride = structureByteStride == 0 ? buffer->get_stride() : structureByteStride;
        const uint64_t availableElements = buffer->get_num_elements();
        if (firstElement > availableElements)
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "UAV first element is out of range.");
        }
        const uint64_t resolvedNumElements = numElements == 0 ? (availableElements - firstElement) : static_cast<uint64_t>(numElements);
        if (stride == 0 || resolvedNumElements == 0 || firstElement + resolvedNumElements > availableElements)
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "UAV buffer range is invalid.");
        }

        D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        desc.Buffer.FirstElement = firstElement;
        desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        desc.Buffer.NumElements = static_cast<UINT>(resolvedNumElements);
        desc.Buffer.StructureByteStride = stride;
        desc.Buffer.CounterOffsetInBytes = 0;

        auto cpuH = get_cpu_handle(id);
        m_renderDevice.get_d3d12_device()->CreateUnorderedAccessView(buffer->get_resource(), nullptr, &desc, cpuH);
        copy_to_gpu_heap(id);
        return Result::ok();
    }
    Result DescriptorAllocator::create_uav_raw_buffer(TableID& id, GpuBufferResource* buffer)
    {
        return create_uav_raw_buffer(id, buffer, 0, 0);
    }
    Result DescriptorAllocator::create_uav_raw_buffer(TableID& id, GpuBufferResource* buffer, uint64_t firstElement, uint32_t numElements)
    {
        // 1) Raw UAV も SRV と同じ 32-bit 要素単位で扱い、buffer 全体と部分 view の両方を許可する。
        if (!id.valid())
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "Invalid TableID.");
        }
        if (buffer == nullptr)
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "Buffer resource is null.");
        }

        const uint64_t availableElements = buffer->get_byte_size() / sizeof(uint32_t);
        if (firstElement > availableElements)
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "Raw UAV first element is out of range.");
        }
        const uint64_t resolvedNumElements = numElements == 0 ? (availableElements - firstElement) : static_cast<uint64_t>(numElements);
        if (resolvedNumElements == 0 || firstElement + resolvedNumElements > availableElements)
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "Raw UAV buffer range is invalid.");
        }

        D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
        desc.Format = DXGI_FORMAT_R32_TYPELESS;
        desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        desc.Buffer.FirstElement = firstElement;
        desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        desc.Buffer.NumElements = static_cast<UINT>(resolvedNumElements);
        desc.Buffer.StructureByteStride = 0;
        desc.Buffer.CounterOffsetInBytes = 0;

        auto cpuH = get_cpu_handle(id);
        m_renderDevice.get_d3d12_device()->CreateUnorderedAccessView(buffer->get_resource(), nullptr, &desc, cpuH);
        copy_to_gpu_heap(id);
        return Result::ok();
    }
    Result DescriptorAllocator::create_srv_texture_2d(TableID& id, GpuTextureResource* texture, DXGI_FORMAT format)
    {
        return create_srv_texture_2d(id, texture, format, 0, 1);
    }
    Result DescriptorAllocator::create_srv_texture_2d(TableID& id, GpuTextureResource* texture, DXGI_FORMAT format, uint32_t mipSlice, uint32_t mipLevels)
    {
        // 1) texture SRV は mip 単位で切れるようにし、1 リソースから複数の表示レベルを作れるようにする。
        if (!id.valid())
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "Invalid TableID.");
        }
        if (texture == nullptr)
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "Texture resource is null.");
        }

        const uint32_t totalMipLevels = texture->get_resource_desc().MipLevels == 0 ? 1u : texture->get_resource_desc().MipLevels;
        if (mipSlice >= totalMipLevels)
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "Texture SRV mip slice is out of range.");
        }
        const uint32_t resolvedMipLevels = mipLevels == 0 ? (totalMipLevels - mipSlice) : mipLevels;
        if (resolvedMipLevels == 0 || mipSlice + resolvedMipLevels > totalMipLevels)
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "Texture SRV mip range is invalid.");
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.Format = format;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MostDetailedMip = mipSlice;
        desc.Texture2D.MipLevels = resolvedMipLevels;

        auto cpuH = get_cpu_handle(id);
        m_renderDevice.get_d3d12_device()->CreateShaderResourceView(texture->get_resource(), &desc, cpuH);
        copy_to_gpu_heap(id);
        return Result::ok();
    }
    Result DescriptorAllocator::create_uav_texture_2d(TableID& id, GpuTextureResource* texture, DXGI_FORMAT format, uint32_t mipSlice)
    {
        // 1) UAV texture は単一 mip に対してだけ張れるので、対象 slice のみを受け付ける。
        if (!id.valid())
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "Invalid TableID.");
        }
        if (texture == nullptr)
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "Texture resource is null.");
        }
        const uint32_t totalMipLevels = texture->get_resource_desc().MipLevels == 0 ? 1u : texture->get_resource_desc().MipLevels;
        if (mipSlice >= totalMipLevels)
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "Texture UAV mip slice is out of range.");
        }

        D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
        desc.Format = format;
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipSlice = mipSlice;

        auto cpuH = get_cpu_handle(id);
        m_renderDevice.get_d3d12_device()->CreateUnorderedAccessView(texture->get_resource(), nullptr, &desc, cpuH);
        copy_to_gpu_heap(id);
        return Result::ok();
    }
    Result DescriptorAllocator::create_rtv(TableID& id, GpuTextureResource* texture, DXGI_FORMAT format)
    {
        return create_rtv(id, texture, format, 0);
    }
    Result DescriptorAllocator::create_rtv(TableID& id, GpuTextureResource* texture, DXGI_FORMAT format, uint32_t mipSlice)
    {
        // 1) RTV も mip ごとに切れるようにして、将来の downsample pass でも再利用できるようにする。
        if (!id.valid())
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "Invalid TableID.");
        }
        if (texture == nullptr)
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "Texture resource is null.");
        }
        const uint32_t totalMipLevels = texture->get_resource_desc().MipLevels == 0 ? 1u : texture->get_resource_desc().MipLevels;
        if (mipSlice >= totalMipLevels)
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "Texture RTV mip slice is out of range.");
        }

        D3D12_RENDER_TARGET_VIEW_DESC desc = {};
        desc.Format = format;
        desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipSlice = mipSlice;

        auto cpuH = get_cpu_handle(id);
        m_renderDevice.get_d3d12_device()->CreateRenderTargetView(texture->get_resource(), &desc, cpuH);
        return Result::ok();
    }
    Result DescriptorAllocator::create_rtv(TableID& id, ID3D12Resource* resource, DXGI_FORMAT format)
    {
        // 1) ID の妥当性を確認する
        if (!id.valid())
        {
            return Result::fail(
                Facility::GraphicsCore,
                Code::InvalidArg,
                Severity::Error,
                0,
                "Invalid TableID.");
        }

        // 2) レンダーターゲットビューの記述子を作成する
        D3D12_RENDER_TARGET_VIEW_DESC desc = {};
        desc.Format = format;
        desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D; // 2D テクスチャとして書き込む

        // 3) CPU ヒープへ登録する
        auto cpuH = get_cpu_handle(id);
        m_renderDevice.get_d3d12_device()->CreateRenderTargetView(resource, &desc, cpuH);

        return Result::ok();
    }
    Result DescriptorAllocator::create_dsv(TableID& id, GpuTextureResource* texture, DXGI_FORMAT format)
    {
        return create_dsv(id, texture, format, 0);
    }
    Result DescriptorAllocator::create_dsv(TableID& id, GpuTextureResource* texture, DXGI_FORMAT format, uint32_t mipSlice)
    {
        // 1) DSV の mip 指定範囲を検証する。
        if (!id.valid())
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "Invalid TableID.");
        }
        if (texture == nullptr)
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "Texture resource is null.");
        }
        const uint32_t totalMipLevels = texture->get_resource_desc().MipLevels == 0 ? 1u : texture->get_resource_desc().MipLevels;
        if (mipSlice >= totalMipLevels)
        {
            return Result::fail(Facility::GraphicsCore, Code::InvalidArg, Severity::Error, 0, "Texture DSV mip slice is out of range.");
        }

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = format;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
        dsvDesc.Texture2D.MipSlice = mipSlice;

        auto cpuH = get_cpu_handle(id);
        m_renderDevice.get_d3d12_device()->CreateDepthStencilView(texture->get_resource(), &dsvDesc, cpuH);
        return Result::ok();
    }
    D3D12_GPU_DESCRIPTOR_HANDLE DescriptorAllocator::get_table_base_gpu(TableKind k)
    {
        // 1) テーブル情報を取得する
        Table& t = get_table(k);

        // 2) ヒープ種別に応じて GPU ハンドルを返す
        D3D12_GPU_DESCRIPTOR_HANDLE baseHandle{};
        if (t.m_heapType == HeapType::CBV_SRV_UAV)
        {
            // CBV_SRV_UAV テーブルは GPU 可視ヒープからのオフセットで返す
            baseHandle = m_gpuSrvUavHeap->GetGPUDescriptorHandleForHeapStart();
        }
        else
        {
            // その他のテーブルは対応するヒープからのオフセットで返す
            baseHandle = m_descriptorHeaps[static_cast<size_t>(t.m_heapType)]->GetGPUDescriptorHandleForHeapStart();
        }

        baseHandle.ptr += static_cast<SIZE_T>(m_descriptorSizes[static_cast<size_t>(t.m_heapType)]) * (t.m_baseIndex);

        return baseHandle;
    }
    D3D12_GPU_DESCRIPTOR_HANDLE DescriptorAllocator::get_gpu_handle(TableID id)
    {
        // 1) ID の妥当性を確認する
        if (!id.valid())
        {
            return D3D12_GPU_DESCRIPTOR_HANDLE_NULL;
        }

        // 2) テーブル情報を取得する
        Table& t = get_table(id.m_kind);

        // 3) ヒープ種別に応じて GPU ハンドルを返す
        D3D12_GPU_DESCRIPTOR_HANDLE handle{};
        if (t.m_heapType == HeapType::CBV_SRV_UAV)
        {
            // CBV_SRV_UAV テーブルは GPU 可視ヒープからのオフセットで返す
            handle = m_gpuSrvUavHeap->GetGPUDescriptorHandleForHeapStart();
        }
        else
        {
            // その他のテーブルは対応するヒープからのオフセットで返す
            handle = m_descriptorHeaps[static_cast<size_t>(t.m_heapType)]->GetGPUDescriptorHandleForHeapStart();
        }

        handle.ptr += static_cast<SIZE_T>(m_descriptorSizes[static_cast<size_t>(t.m_heapType)]) * (t.m_baseIndex + id.m_index);

        return handle;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE DescriptorAllocator::get_cpu_handle_gpu_visible(TableID id)
    {
        // 1) ID の妥当性を確認する
        if (!id.valid())
        {
            return D3D12_CPU_DESCRIPTOR_HANDLE_NULL;
        }

        // 2) テーブル情報を取得する
        Table& t = get_table(id.m_kind);

        // 3) GPU 可視ヒープからのオフセットで CPU ハンドルを返す
        D3D12_CPU_DESCRIPTOR_HANDLE handle = m_gpuSrvUavHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(m_descriptorSizes[static_cast<size_t>(t.m_heapType)]) * (t.m_baseIndex + id.m_index);

        return handle;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE DescriptorAllocator::get_cpu_handle(TableID id)
    {
        // 1) ID の妥当性を確認する
        if (!id.valid())
        {
            return D3D12_CPU_DESCRIPTOR_HANDLE_NULL;
        }

        // 2) テーブル情報を取得する
        Table& t = get_table(id.m_kind);

        // 3) ヒープ種別に応じて CPU ハンドルを返す
        D3D12_CPU_DESCRIPTOR_HANDLE handle{};
        handle = m_descriptorHeaps[static_cast<size_t>(t.m_heapType)]->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(m_descriptorSizes[static_cast<size_t>(t.m_heapType)]) * (t.m_baseIndex + id.m_index);

        return handle;

    }
    void DescriptorAllocator::ensure_capacity(TableKind k, uint32_t needOneMore)
    {
        (void)k;
        (void)needOneMore;
    }
    void DescriptorAllocator::recreate_heap(TableKind k, uint32_t newCap, uint32_t newBufCap)
    {
        (void)k;
        (void)newCap;
        (void)newBufCap;
    }
    DescriptorAllocator::Table& DescriptorAllocator::get_table(TableKind k)
    {
        // 1) 種類に応じてテーブルを返す
        switch (k)
        {
        case TableKind::Textures:
            return m_textures;
        case TableKind::Buffers:
            return m_buffers;
        case TableKind::RenderTargets:
            return m_renderTargets;
        case TableKind::DepthStencils:
            return m_depthStencils;
        default:
            Assert::cue_assert(false, "DescriptorAllocator", "Unknown TableKind");
            break;
        }

        // 2) 無効なテーブルを返す
        return m_textures; // どれでもいいので返す
    }
    void DescriptorAllocator::copy_to_gpu_heap(const TableID& id)
    {
        // 1) ID の妥当性を確認する
        if (!id.valid())
        {
            return;
        }

        // 2) テーブル情報を取得する
        Table& t = get_table(id.m_kind);

        // 3) GPU 可視ヒープへコピーする（CBV_SRV_UAV テーブルのみ）
        if (t.m_heapType != HeapType::CBV_SRV_UAV)
        {
            return;
        }

        // 4) コピー元とコピー先のハンドルを取得する
        D3D12_CPU_DESCRIPTOR_HANDLE srcHandle = get_cpu_handle(id);
        D3D12_CPU_DESCRIPTOR_HANDLE dst = m_gpuSrvUavHeap->GetCPUDescriptorHandleForHeapStart();

        // 5) コピー先のオフセットを計算する
        const SIZE_T inc =
            static_cast<SIZE_T>(m_descriptorSizes[static_cast<size_t>(HeapType::CBV_SRV_UAV)]);
        const uint32_t slotIndex = t.m_baseIndex + id.m_index;

        dst.ptr += inc * slotIndex;

        // 6) コピー
        m_renderDevice.get_d3d12_device()->CopyDescriptorsSimple(
            1,
            dst, // コピー先 (GPU ヒープ)
            srcHandle, // コピー元 (CPU ヒープ)
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
} // namespace Cue::GraphicsCore::DX12
