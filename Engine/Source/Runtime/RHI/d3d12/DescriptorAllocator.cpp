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
        // 1) 先にヒープを確保しておくことで、実行時の断片化や再確保コストを避けます。
        for (size_t i = 0; i < static_cast<size_t>(TableKind::DepthStencils) + 1; ++i)
        {
            HeapType heapType = static_cast<HeapType>(i);
            switch (heapType)
            {
            case HeapType::CBV_SRV_UAV:
                create_descriptor_heap(heapType, a_texCap + a_bufCap, false);
                create_descriptor_heap(heapType, a_texCap + a_bufCap, true);

                // 2) テーブルごとに固定領域へ切ることで、GPU 可視ヒープとの対応関係を単純化します。
                m_textures.m_heapType = heapType;
                m_textures.m_capacity = a_texCap;
                m_textures.m_baseIndex = 0;
                m_textures.m_freeList.reserve(a_texCap);
                for (uint32_t j = 0; j < a_texCap; ++j)
                {
                    m_textures.m_freeList.push_back(a_texCap - 1u - j);
                }
                
                m_buffers.m_heapType = heapType;
                m_buffers.m_capacity = a_bufCap;
                m_buffers.m_baseIndex = a_texCap; // テクスチャの後ろにバッファを配置
                m_buffers.m_freeList.reserve(a_bufCap);
                for (uint32_t j = 0; j < a_bufCap; ++j)
                {
                    m_buffers.m_freeList.push_back(a_bufCap - 1u - j);
                }
                break;
            case HeapType::SAMPLER:
                break;
            case HeapType::RTV:
                create_descriptor_heap(heapType, a_rtvCap, false);

                m_renderTargets.m_heapType = heapType;
                m_renderTargets.m_capacity = a_rtvCap;
                m_renderTargets.m_baseIndex = 0;
                m_renderTargets.m_freeList.reserve(a_rtvCap);
                for (uint32_t j = 0; j < a_rtvCap; ++j)
                {
                    m_renderTargets.m_freeList.push_back(a_rtvCap - 1u - j);
                }
                break;
            case HeapType::DSV:
                create_descriptor_heap(heapType, a_dsvCap, false);

                m_depthStencils.m_heapType = heapType;
                m_depthStencils.m_capacity = a_dsvCap;
                m_depthStencils.m_baseIndex = 0;
                m_depthStencils.m_freeList.reserve(a_dsvCap);
                for (uint32_t j = 0; j < a_dsvCap; ++j)
                {
                    m_depthStencils.m_freeList.push_back(a_dsvCap - 1u - j);
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
        // 1) 空きを先に確認しないと、テーブル枯渇を正常な失敗として返せません。
        Table& t = get_table(a_kind);
        if (t.m_freeList.empty())
        {
            CUE_ASSERT_MSG(false, "Descriptor table full, need to expand.");
            return TableID{ a_kind, t.m_generation, TableID::kInvalid };
        }

        // 2) LIFO で返すと最近使った領域を再利用しやすく、管理も最小です。
        uint32_t idx = t.m_freeList.back();
        t.m_freeList.pop_back();
        return TableID{ a_kind, t.m_generation, idx };
    }
    void DescriptorAllocator::free_table(TableID a_id)
    {
        // 1) 無効 ID を弾いて、二重解放や未初期化値の混入を防ぎます。
        if (!a_id.valid())
        {
            return;
        }

        // 2) 空きリストへ戻して、次回割り当て時に再利用可能にします。
        Table& t = get_table(a_id.m_kind);
        t.m_freeList.push_back(a_id.m_index);
    }
    D3D12_GPU_DESCRIPTOR_HANDLE DescriptorAllocator::get_table_base_gpu(TableKind a_kind)
    {
        // 1) テーブル単位の基点を返すことで、呼び出し側が相対インデックスだけで扱えます。
        Table& t = get_table(a_kind);

        D3D12_GPU_DESCRIPTOR_HANDLE baseHandle{};
        if (t.m_heapType == HeapType::CBV_SRV_UAV)
        {
            baseHandle = m_gpuSrvUavDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
        }
        else
        {
            baseHandle = m_descriptorHeaps[static_cast<size_t>(t.m_heapType)]->GetGPUDescriptorHandleForHeapStart();
        }

        baseHandle.ptr += static_cast<SIZE_T>(m_descriptorSizes[static_cast<size_t>(t.m_heapType)]) * (t.m_baseIndex);

        return baseHandle;
    }
    D3D12_GPU_DESCRIPTOR_HANDLE DescriptorAllocator::get_gpu_handle(TableID a_id)
    {
        // 1) 無効 ID をそのまま GPU へ流すとクラッシュ検知が遅れるため、ここで止めます。
        if (!a_id.valid())
        {
            return D3D12_GPU_DESCRIPTOR_HANDLE_NULL;
        }

        // 2) CPU/GPU で同じスロット計算式を使うため、ここでテーブル情報へ正規化します。
        Table& t = get_table(a_id.m_kind);

        D3D12_GPU_DESCRIPTOR_HANDLE handle{};
        if (t.m_heapType == HeapType::CBV_SRV_UAV)
        {
            handle = m_gpuSrvUavDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
        }
        else
        {
            handle = m_descriptorHeaps[static_cast<size_t>(t.m_heapType)]->GetGPUDescriptorHandleForHeapStart();
        }

        handle.ptr += static_cast<SIZE_T>(m_descriptorSizes[static_cast<size_t>(t.m_heapType)]) * (t.m_baseIndex + a_id.m_index);

        return handle;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE DescriptorAllocator::get_cpu_handle_gpu_visible(TableID a_id)
    {
        // 1) GPU 可視ヒープでも、無効 ID を通すとコピー先計算を壊すため先に除外します。
        if (!a_id.valid())
        {
            return D3D12_CPU_DESCRIPTOR_HANDLE_NULL;
        }

        Table& t = get_table(a_id.m_kind);

        D3D12_CPU_DESCRIPTOR_HANDLE handle = m_gpuSrvUavDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(m_descriptorSizes[static_cast<size_t>(t.m_heapType)]) * (t.m_baseIndex + a_id.m_index);

        return handle;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE DescriptorAllocator::get_cpu_handle(TableID a_id)
    {
        // 1) CPU ヒープでも同じ ID 検証を揃えて、呼び出し側の前提を簡潔にします。
        if (!a_id.valid())
        {
            return D3D12_CPU_DESCRIPTOR_HANDLE_NULL;
        }

        Table& t = get_table(a_id.m_kind);

        D3D12_CPU_DESCRIPTOR_HANDLE handle{};
        handle = m_descriptorHeaps[static_cast<size_t>(t.m_heapType)]->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(m_descriptorSizes[static_cast<size_t>(t.m_heapType)]) * (t.m_baseIndex + a_id.m_index);

        return handle;
    }

    ID3D12DescriptorHeap* DescriptorAllocator::get_descriptor_heap(HeapType a_type) const noexcept
    {
        // 1) GPU 可視 heap を要求する種別だけ分岐させると、呼び出し側の条件分岐を減らせます。
        if (a_type == HeapType::CBV_SRV_UAV)
        {
            return m_gpuSrvUavDescriptorHeap.Get();
        }
        return m_descriptorHeaps[static_cast<size_t>(a_type)].Get();
    }

    Result DescriptorAllocator::compute_descriptor_sizes()
    {
        // 1) ヒープごとの増分サイズを先に固定しておくと、後続のハンドル計算を整数演算だけで済ませられます。
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
        // 1) 抽象 enum をここで D3D12 型へ落とし込むと、呼び出し側を API 非依存に保てます。
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
        // 1) テーブル選択を集約しておくと、呼び出し側で分岐が散らばりません。
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
        // 1) 無効 ID を早期に捨てて、ヒープ外アクセスの原因をここで止めます。
        if (!a_id.valid())
        {
            return;
        }

        Table& t = get_table(a_id.m_kind);

        // 2) GPU 可視ヒープに意味があるのは CBV/SRV/UAV だけなので、それ以外は処理しません。
        if (t.m_heapType != HeapType::CBV_SRV_UAV)
        {
            return;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE srcHandle = get_cpu_handle(a_id);
        D3D12_CPU_DESCRIPTOR_HANDLE dst = m_gpuSrvUavDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

        const SIZE_T inc =
            static_cast<SIZE_T>(m_descriptorSizes[static_cast<size_t>(HeapType::CBV_SRV_UAV)]);
        const uint32_t slotIndex = t.m_baseIndex + a_id.m_index;

        dst.ptr += inc * slotIndex;

        m_device.CopyDescriptorsSimple(
            1,
            dst,
            srcHandle,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    Result DescriptorAllocator::create_cbv(TableID id, DX12GpuResource* resource, uint64_t byteOffset, uint32_t byteSize)
    {
        // 定数バッファ view の範囲を実体サイズへ合わせる。
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
