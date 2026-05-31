#include "DescriptorAllocator.h"

// === C++ includes ===
#include <string>

namespace Cue::RHI::DX12
{
    namespace
    {
        UINT align_constant_buffer_size(UINT a_size)
        {
            return (a_size + 255u) & ~255u;
        }

        D3D12_DESCRIPTOR_HEAP_TYPE to_d3d12_heap_type(HeapType a_heapType)
        {
            switch (a_heapType)
            {
            case HeapType::CBV_SRV_UAV:
                return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            case HeapType::SAMPLER:
                return D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
            case HeapType::RTV:
                return D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            case HeapType::DSV:
                return D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
            default:
                CUE_ASSERT_MSG(false, "Invalid HeapType");
                return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; // デフォルト値
            }
        }

        std::wstring to_string(HeapType a_heapType)
        {
            switch (a_heapType)
            {
            case HeapType::CBV_SRV_UAV:
                return L"CBV_SRV_UAV";
            case HeapType::SAMPLER:
                return L"SAMPLER";
            case HeapType::RTV:
                return L"RTV";
            case HeapType::DSV:
                return L"DSV";
            default:
                CUE_ASSERT_MSG(false, "Invalid HeapType");
                return L"Unknown"; // デフォルト値
            }
        }
    }

    Result DescriptorAllocator::initialize(uint32_t a_texCap, uint32_t a_bufCap, uint32_t a_rtvCap, uint32_t a_dsvCap)
    {
        // デスクリプタサイズを取得しておき
        Result result = compute_descriptor_sizes();
        if (!result)
        {
            return result;
        }

        // 先にヒープを確保しておくことで、実行時の断片化や再確保コストを避ける
        for (size_t i = 0; i < static_cast<size_t>(TableKind::DepthStencils) + 1; ++i)
        {
            HeapType heapType = static_cast<HeapType>(i);
            switch (heapType)
            {
            case HeapType::CBV_SRV_UAV:
                create_descriptor_heap(heapType, a_texCap + a_bufCap, false);
                create_descriptor_heap(heapType, a_texCap + a_bufCap, true);

                // テーブルごとに固定領域へ切ることで、GPU 可視ヒープとの対応関係を単純化し
                m_textures.heapType = heapType;
                m_textures.capacity = a_texCap;
                m_textures.baseIndex = 0;
                m_textures.freeList.reserve(a_texCap);
                for (uint32_t j = 0; j < a_texCap; ++j)
                {
                    m_textures.freeList.push_back(a_texCap - 1u - j);
                }

                m_buffers.heapType = heapType;
                m_buffers.capacity = a_bufCap;
                m_buffers.baseIndex = a_texCap; // テクスチャの後ろにバッファを配置
                m_buffers.freeList.reserve(a_bufCap);
                for (uint32_t j = 0; j < a_bufCap; ++j)
                {
                    m_buffers.freeList.push_back(a_bufCap - 1u - j);
                }
                break;
            case HeapType::SAMPLER:
                break;
            case HeapType::RTV:
                create_descriptor_heap(heapType, a_rtvCap, false);

                m_renderTargets.heapType = heapType;
                m_renderTargets.capacity = a_rtvCap;
                m_renderTargets.baseIndex = 0;
                m_renderTargets.freeList.reserve(a_rtvCap);
                for (uint32_t j = 0; j < a_rtvCap; ++j)
                {
                    m_renderTargets.freeList.push_back(a_rtvCap - 1u - j);
                }
                break;
            case HeapType::DSV:
                create_descriptor_heap(heapType, a_dsvCap, false);

                m_depthStencils.heapType = heapType;
                m_depthStencils.capacity = a_dsvCap;
                m_depthStencils.baseIndex = 0;
                m_depthStencils.freeList.reserve(a_dsvCap);
                for (uint32_t j = 0; j < a_dsvCap; ++j)
                {
                    m_depthStencils.freeList.push_back(a_dsvCap - 1u - j);
                }
                break;
            case HeapType::kCount:
            default:
                CUE_ASSERT_MSG(false, "Invalid HeapType");
                break;
            }
        }

        return Result::ok();
    }
    TableID DescriptorAllocator::allocate(TableKind a_kind)
    {
        // - 空きを先に確認しないと、テーブル枯渇を正常な失敗として返せません
        Table& t = get_table(a_kind);
        if (t.freeList.empty())
        {
            CUE_ASSERT_MSG(false, "Descriptor table full, need to expand.");
            return TableID{ a_kind, t.generation, TableID::kInvalid };
        }

        // - LIFO で返すと最近使った領域を再利用しやすく、管理も最小
        uint32_t idx = t.freeList.back();
        t.freeList.pop_back();
        return TableID{ a_kind, t.generation, idx };
    }
    void DescriptorAllocator::free_table(TableID a_id)
    {
        // - 無効 ID を弾いて、二重解放や未初期化値の混入を防ぐ
        if (!a_id.valid())
        {
            return;
        }

        // - 空きリストへ戻して、次回割り当て時に再利用可能にし
        Table& t = get_table(a_id.kind);
        t.freeList.push_back(a_id.index);
    }
    D3D12_GPU_DESCRIPTOR_HANDLE DescriptorAllocator::get_table_base_gpu(TableKind a_kind)
    {
        // - テーブル単位の基点を返すことで、呼び出し側が相対インデックスだけで扱え
        Table& t = get_table(a_kind);

        D3D12_GPU_DESCRIPTOR_HANDLE baseHandle{};
        if (t.heapType == HeapType::CBV_SRV_UAV)
        {
            baseHandle = m_gpuSrvUavDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
        }
        else
        {
            baseHandle = m_descriptorHeaps[static_cast<size_t>(t.heapType)]->GetGPUDescriptorHandleForHeapStart();
        }

        baseHandle.ptr += static_cast<SIZE_T>(m_descriptorSizes[static_cast<size_t>(t.heapType)]) * (t.baseIndex);

        return baseHandle;
    }
    D3D12_GPU_DESCRIPTOR_HANDLE DescriptorAllocator::get_gpu_handle(TableID a_id)
    {
        // - 無効 ID をそのまま GPU へ流すとクラッシュ検知が遅れるため、ここで止め
        if (!a_id.valid())
        {
            return D3D12_GPU_DESCRIPTOR_HANDLE_NULL;
        }

        // - CPU/GPU で同じスロット計算式を使うため、ここでテーブル情報へ正規化し
        Table& t = get_table(a_id.kind);

        D3D12_GPU_DESCRIPTOR_HANDLE handle{};
        if (t.heapType == HeapType::CBV_SRV_UAV)
        {
            handle = m_gpuSrvUavDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
        }
        else
        {
            handle = m_descriptorHeaps[static_cast<size_t>(t.heapType)]->GetGPUDescriptorHandleForHeapStart();
        }

        handle.ptr += static_cast<SIZE_T>(m_descriptorSizes[static_cast<size_t>(t.heapType)]) * (t.baseIndex + a_id.index);

        return handle;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE DescriptorAllocator::get_cpu_handle_gpu_visible(TableID a_id)
    {
        // - GPU 可視ヒープでも、無効 ID を通すとコピー先計算を壊すため先に除外し
        if (!a_id.valid())
        {
            return D3D12_CPU_DESCRIPTOR_HANDLE_NULL;
        }

        Table& t = get_table(a_id.kind);

        D3D12_CPU_DESCRIPTOR_HANDLE handle = m_gpuSrvUavDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(m_descriptorSizes[static_cast<size_t>(t.heapType)]) * (t.baseIndex + a_id.index);

        return handle;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE DescriptorAllocator::get_cpu_handle(TableID a_id)
    {
        // - CPU ヒープでも同じ ID 検証を揃えて、呼び出し側の前提を簡潔にし
        if (!a_id.valid())
        {
            return D3D12_CPU_DESCRIPTOR_HANDLE_NULL;
        }

        Table& t = get_table(a_id.kind);

        D3D12_CPU_DESCRIPTOR_HANDLE handle{};
        handle = m_descriptorHeaps[static_cast<size_t>(t.heapType)]->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(m_descriptorSizes[static_cast<size_t>(t.heapType)]) * (t.baseIndex + a_id.index);

        return handle;
    }

    Result DescriptorAllocator::allocate_shader_visible_texture_descriptor(
        D3D12_CPU_DESCRIPTOR_HANDLE& a_outCpuHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE& a_outGpuHandle)
    {
        TableID id = allocate(TableKind::Textures);
        if (!id.valid())
        {
            return Result::fail(
                Code::OutOfMemory,
                Severity::Error,
                "Failed to allocate shader-visible texture descriptor.");
        }

        a_outCpuHandle = get_cpu_handle_gpu_visible(id);
        a_outGpuHandle = get_gpu_handle(id);
        return Result::ok();
    }

    void DescriptorAllocator::free_shader_visible_texture_descriptor(
        D3D12_CPU_DESCRIPTOR_HANDLE a_cpuHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE a_gpuHandle)
    {
        if (!m_gpuSrvUavDescriptorHeap || a_cpuHandle.ptr == 0 || a_gpuHandle.ptr == 0)
        {
            return;
        }

        Table& textureTable = get_table(TableKind::Textures);
        const SIZE_T descriptorSize =
            static_cast<SIZE_T>(m_descriptorSizes[static_cast<size_t>(HeapType::CBV_SRV_UAV)]);
        if (descriptorSize == 0)
        {
            return;
        }

        const D3D12_CPU_DESCRIPTOR_HANDLE heapCpuStart =
            m_gpuSrvUavDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        const D3D12_GPU_DESCRIPTOR_HANDLE heapGpuStart =
            m_gpuSrvUavDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

        if (a_cpuHandle.ptr < heapCpuStart.ptr || a_gpuHandle.ptr < heapGpuStart.ptr)
        {
            return;
        }

        const SIZE_T cpuOffset = a_cpuHandle.ptr - heapCpuStart.ptr;
        const UINT64 gpuOffset = a_gpuHandle.ptr - heapGpuStart.ptr;
        if ((cpuOffset % descriptorSize) != 0 || (gpuOffset % descriptorSize) != 0)
        {
            return;
        }

        const uint32_t cpuSlot = static_cast<uint32_t>(cpuOffset / descriptorSize);
        const uint32_t gpuSlot = static_cast<uint32_t>(gpuOffset / descriptorSize);
        if (cpuSlot != gpuSlot)
        {
            return;
        }

        if (cpuSlot < textureTable.baseIndex)
        {
            return;
        }

        const uint32_t localIndex = cpuSlot - textureTable.baseIndex;
        if (localIndex >= textureTable.capacity)
        {
            return;
        }

        free_table(TableID{ TableKind::Textures, textureTable.generation, localIndex });
    }

    ID3D12DescriptorHeap* DescriptorAllocator::get_descriptor_heap(HeapType a_type) const noexcept
    {
        // - GPU 可視 heap を要求する種別だけ分岐させると、呼び出し側の条件分岐を減らせ
        if (a_type == HeapType::CBV_SRV_UAV)
        {
            return m_gpuSrvUavDescriptorHeap.Get();
        }
        return m_descriptorHeaps[static_cast<size_t>(a_type)].Get();
    }

    Result DescriptorAllocator::compute_descriptor_sizes()
    {
        // - ヒープごとの増分サイズを先に固定しておくと、後続のハンドル計算を整数演算だけで済ませられ
        for (size_t i = 0; i < static_cast<size_t>(HeapType::kCount); ++i)
        {
            HeapType heapType = static_cast<HeapType>(i);

            D3D12_DESCRIPTOR_HEAP_TYPE d3dHeapType = to_d3d12_heap_type(heapType);

            m_descriptorSizes[i] = m_device.GetDescriptorHandleIncrementSize(d3dHeapType);
        }

        return Result::ok();
    }
    Result DescriptorAllocator::create_descriptor_heap(HeapType a_heapType, uint32_t a_size, bool a_shaderVisible)
    {
        // - 抽象 enum をここで D3D12 型へ落とし込むと、呼び出し側を API 非依存に保て
        D3D12_DESCRIPTOR_HEAP_TYPE d3dHeapType = to_d3d12_heap_type(a_heapType);
        D3D12_DESCRIPTOR_HEAP_DESC desc{};

        desc.Type = d3dHeapType;
        desc.NumDescriptors = a_size;
        if (a_shaderVisible && a_heapType == HeapType::CBV_SRV_UAV)
        {
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            m_device.CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_gpuSrvUavDescriptorHeap));
            set_d3d12_name(m_gpuSrvUavDescriptorHeap.Get(), (L"GPU " + to_string(a_heapType) + L" Descriptor Heap").c_str());
        }
        else
        {
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            m_device.CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_descriptorHeaps[static_cast<size_t>(a_heapType)]));
            set_d3d12_name(m_descriptorHeaps[static_cast<size_t>(a_heapType)].Get(), (L"CPU " + to_string(a_heapType) + L" Descriptor Heap").c_str());
        }

        return Result::ok();
    }
    DescriptorAllocator::Table& DescriptorAllocator::get_table(TableKind a_kind)
    {
        // - テーブル選択を集約しておくと、呼び出し側で分岐が散らばりません
        switch (a_kind)
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
            CUE_ASSERT_MSG(false, "Invalid TableKind");
            break;
        }

        return m_textures; // どれでもいいので返す
    }
    void DescriptorAllocator::copy_to_gpu_heap(TableID a_id)
    {
        // - 無効 ID を早期に捨てて、ヒープ外アクセスの原因をここで止め
        if (!a_id.valid())
        {
            return;
        }

        Table& t = get_table(a_id.kind);

        // - GPU 可視ヒープに意味があるのは CBV/SRV/UAV だけなので、それ以外は処理しません
        if (t.heapType != HeapType::CBV_SRV_UAV)
        {
            return;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE srcHandle = get_cpu_handle(a_id);
        D3D12_CPU_DESCRIPTOR_HANDLE dst = m_gpuSrvUavDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

        const SIZE_T inc =
            static_cast<SIZE_T>(m_descriptorSizes[static_cast<size_t>(HeapType::CBV_SRV_UAV)]);
        const uint32_t slotIndex = t.baseIndex + a_id.index;

        dst.ptr += inc * slotIndex;

        m_device.CopyDescriptorsSimple(
            1,
            dst,
            srcHandle,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    Result DescriptorAllocator::create_cbv(TableID id, DX12GpuResource* resource, uint64_t byteOffset, uint32_t byteSize)
    {
        // 定数バッファ view の範囲を実体サイズへ合わせる
        if (!id.valid())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "Invalid TableID.");
        }
        if (resource == nullptr)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "Buffer resource is null.");
        }

        const uint64_t bufferSize = resource->get_buffer_size();
        if (byteOffset > bufferSize)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "CBV byte offset is out of range.");
        }

        const uint64_t resolvedSize =
            byteSize == 0 ? (bufferSize - byteOffset) : static_cast<uint64_t>(byteSize);

        if (resolvedSize == 0)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "CBV byte range is invalid.");
        }

        const uint64_t alignedSize =
            static_cast<uint64_t>((static_cast<UINT>(resolvedSize) + 255u) & ~255u);

        if (byteOffset + alignedSize > bufferSize)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "CBV aligned byte range exceedsbuffer size.");
        }

        D3D12_CONSTANT_BUFFER_VIEW_DESC desc{};
        desc.BufferLocation = resource->get_resource()->GetGPUVirtualAddress() + byteOffset;
        desc.SizeInBytes = (static_cast<UINT>(resolvedSize) + 255u) & ~255u;

        auto cpuH = get_cpu_handle(id);
        m_device.CreateConstantBufferView(&desc, cpuH);
        copy_to_gpu_heap(id);
        return Result::ok();
    }

    Result DescriptorAllocator::create_srv_buffer(TableID id, DX12GpuResource* resource, uint64_t firstElement, uint32_t numElements, uint32_t structureByteStride)
    {
        // Structured SRV は element 単位の範囲指定を受け取り、同一 buffer から複数 view を切り出せるようにする
        if (!id.valid())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "Invalid TableID.");
        }
        if (resource == nullptr)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "Buffer resource is null.");
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Buffer.FirstElement = static_cast<UINT64>(firstElement);
        desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        desc.Buffer.NumElements = static_cast<UINT>(numElements);
        desc.Buffer.StructureByteStride = static_cast<UINT>(structureByteStride);

        auto cpuH = get_cpu_handle(id);
        m_device.CreateShaderResourceView(resource->get_resource(), &desc, cpuH);
        copy_to_gpu_heap(id);
        return Result::ok();
    }

    Result DescriptorAllocator::create_srv_raw_buffer(TableID id, DX12GpuResource* resource, uint64_t firstElement, uint32_t numElements)
    {
        // - Raw SRV は 32-bit 要素として切るので、byte size から有効範囲を導出する
        if (!id.valid())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "Invalid TableID.");
        }
        if (resource == nullptr)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "Buffer resource is null.");
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
        desc.Format = DXGI_FORMAT_R32_TYPELESS;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Buffer.FirstElement = static_cast<UINT64>(firstElement);
        desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        desc.Buffer.NumElements = static_cast<UINT>(numElements);
        desc.Buffer.StructureByteStride = 0;

        auto cpuH = get_cpu_handle(id);
        m_device.CreateShaderResourceView(resource->get_resource(), &desc, cpuH);
        copy_to_gpu_heap(id);
        return Result::ok();
    }

    Result DescriptorAllocator::create_uav_buffer(TableID id, DX12GpuResource* resource, uint64_t firstElement, uint32_t numElements, uint32_t structureByteStride)
    {
        // - Structured UAV も range 指定対応にして、同一 buffer の別領域へ独立した書き込み view を張れるようにする
        if (!id.valid())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "Invalid TableID.");
        }
        if (resource == nullptr)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "Buffer resource is null.");
        }

        D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        desc.Buffer.FirstElement = static_cast<UINT64>(firstElement);
        desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        desc.Buffer.NumElements = static_cast<UINT>(numElements);
        desc.Buffer.StructureByteStride = static_cast<UINT>(structureByteStride);
        desc.Buffer.CounterOffsetInBytes = 0;

        auto cpuH = get_cpu_handle(id);
        m_device.CreateUnorderedAccessView(resource->get_resource(), nullptr, &desc, cpuH);
        copy_to_gpu_heap(id);
        return Result::ok();
    }

    Result DescriptorAllocator::create_uav_raw_buffer(TableID id, DX12GpuResource* resource, uint64_t firstElement, uint32_t numElements)
    {
        // - Raw UAV も SRV と同じ 32-bit 要素単位で扱い、buffer 全体と部分 view の両方を許可する
        if (!id.valid())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "Invalid TableID.");
        }
        if (resource == nullptr)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "Buffer resource is null.");
        }

        D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
        desc.Format = DXGI_FORMAT_R32_TYPELESS;
        desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        desc.Buffer.FirstElement = static_cast<UINT64>(firstElement);
        desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        desc.Buffer.NumElements = static_cast<UINT>(numElements);
        desc.Buffer.StructureByteStride = 0;
        desc.Buffer.CounterOffsetInBytes = 0;

        auto cpuH = get_cpu_handle(id);
        m_device.CreateUnorderedAccessView(resource->get_resource(), nullptr, &desc, cpuH);
        copy_to_gpu_heap(id);
        return Result::ok();
    }

    Result DescriptorAllocator::create_srv_texture_2d(TableID id, DX12GpuResource* resource, DXGI_FORMAT format, uint32_t mipSlice, uint32_t mipLevels)
    {
        // - texture SRV は mip 単位で切れるようにし、1 リソースから複数の表示レベルを作れるようにする
        if (!id.valid())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "Invalid TableID.");
        }
        if (resource == nullptr)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "Texture resource is null.");
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.Format = format;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MostDetailedMip = mipSlice;
        desc.Texture2D.MipLevels = mipLevels;

        auto cpuH = get_cpu_handle(id);
        m_device.CreateShaderResourceView(resource->get_resource(), &desc, cpuH);
        copy_to_gpu_heap(id);
        return Result::ok();
    }

    Result DescriptorAllocator::create_srv_texture_cube(TableID id, DX12GpuResource* resource, DXGI_FORMAT format, uint32_t mipSlice, uint32_t mipLevels)
    {
        if (!id.valid())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "Invalid TableID.");
        }
        if (resource == nullptr)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "Texture resource is null.");
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.Format = format;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        desc.TextureCube.MostDetailedMip = mipSlice;
        desc.TextureCube.MipLevels = mipLevels;
        desc.TextureCube.ResourceMinLODClamp = 0.0f;

        auto cpuH = get_cpu_handle(id);
        m_device.CreateShaderResourceView(resource->get_resource(), &desc, cpuH);
        copy_to_gpu_heap(id);
        return Result::ok();
    }

    Result DescriptorAllocator::create_uav_texture_2d(TableID id, DX12GpuResource* resource, DXGI_FORMAT format, uint32_t mipSlice)
    {
        // - UAV texture は単一 mip に対してだけ張れるので、対象 slice のみを受け付ける
        if (!id.valid())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "Invalid TableID.");
        }
        if (resource == nullptr)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "Texture resource is null.");
        }

        D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
        desc.Format = format;
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipSlice = mipSlice;

        auto cpuH = get_cpu_handle(id);
        m_device.CreateUnorderedAccessView(resource->get_resource(), nullptr, &desc, cpuH);
        copy_to_gpu_heap(id);
        return Result::ok();
    }

    Result DescriptorAllocator::create_rtv(TableID id, DX12GpuResource* resource, DXGI_FORMAT format, uint32_t mipSlice)
    {
        // - RTV も mip ごとに切れるようにして、将来の downsample pass でも再利用できるようにする
        if (!id.valid())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "Invalid TableID.");
        }
        if (resource == nullptr)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "Texture resource is null.");
        }

        D3D12_RENDER_TARGET_VIEW_DESC desc = {};
        desc.Format = format;
        desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipSlice = mipSlice;

        auto cpuH = get_cpu_handle(id);
        m_device.CreateRenderTargetView(resource->get_resource(), &desc, cpuH);
        return Result::ok();
    }

    Result DescriptorAllocator::create_dsv(TableID id, DX12GpuResource* resource, DXGI_FORMAT format, uint32_t mipSlice)
    {
        // - DSV の mip 指定範囲を検証する
        if (!id.valid())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "Invalid TableID.");
        }
        if (resource == nullptr)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "Texture resource is null.");
        }

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = format;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
        dsvDesc.Texture2D.MipSlice = mipSlice;

        auto cpuH = get_cpu_handle(id);
        m_device.CreateDepthStencilView(resource->get_resource(), &dsvDesc, cpuH);
        return Result::ok();
    }

    Result DescriptorAllocator::create_rtv(TableID id, ID3D12Resource* resource, DXGI_FORMAT format)
    {
        // ID の妥当性を確認する
        if (!id.valid())
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Invalid TableID.");
        }

        // レンダーターゲットビューの記述子を作成する
        D3D12_RENDER_TARGET_VIEW_DESC desc = {};
        desc.Format = format;
        desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D; // 2D テクスチャとして書き込む

        // CPU ヒープへ登録する
        auto cpuH = get_cpu_handle(id);
        m_device.CreateRenderTargetView(resource, &desc, cpuH);

        return Result::ok();
    }
}
