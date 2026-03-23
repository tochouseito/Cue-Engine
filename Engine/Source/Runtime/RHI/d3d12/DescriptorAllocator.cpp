#include "DescriptorAllocator.h"
#include <string>

namespace Cue::RHI::DX12
{
    namespace
    {
        D3D12_DESCRIPTOR_HEAP_TYPE to_d3d12_heap_type(HeapType heapType)
        {
            switch (heapType)
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

        std::wstring to_string(HeapType heapType)
        {
            switch (heapType)
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

    Result DescriptorAllocator::initialize(uint32_t texCap, uint32_t bufCap, uint32_t rtvCap, uint32_t dsvCap)
    {
        for (size_t i = 0; i < static_cast<size_t>(TableKind::DepthStencils) + 1; ++i)
        {
            HeapType heapType = static_cast<HeapType>(i);
            switch (heapType)
            {
            case HeapType::CBV_SRV_UAV:
                // CBV/SRV/UAV ヒープを作成します（シェーダー非表示）
                create_descriptor_heap(heapType, texCap + bufCap, false);
                // CBV/SRV/UAV ヒープを作成します（シェーダー表示）
                create_descriptor_heap(heapType, texCap + bufCap, true);

                // テクスチャとバッファのテーブルを初期化します
                m_textures.m_heapType = heapType;
                m_textures.m_capacity = texCap;
                m_textures.m_baseIndex = 0;
                m_textures.m_freeList.reserve(texCap);
                for (uint32_t j = 0; j < texCap; ++j)
                {
                    m_textures.m_freeList.push_back(texCap - 1u - j);
                }
                
                m_buffers.m_heapType = heapType;
                m_buffers.m_capacity = bufCap;
                m_buffers.m_baseIndex = texCap;// テクスチャの後ろにバッファを配置
                m_buffers.m_freeList.reserve(bufCap);
                for (uint32_t j = 0; j < bufCap; ++j)
                {
                    m_buffers.m_freeList.push_back(bufCap - 1u - j);
                }
                break;
            case HeapType::SAMPLER:
                /*
                サンプラーヒープは未実装
                */
                break;
            case HeapType::RTV:
                // RTV ヒープを作成します（シェーダー非表示）
                create_descriptor_heap(heapType, rtvCap, false);

                // レンダーターゲットのテーブルを初期化します
                m_renderTargets.m_heapType = heapType;
                m_renderTargets.m_capacity = rtvCap;
                m_renderTargets.m_baseIndex = 0;
                m_renderTargets.m_freeList.reserve(rtvCap);
                for (uint32_t j = 0; j < rtvCap; ++j)
                {
                    m_renderTargets.m_freeList.push_back(rtvCap - 1u - j);
                }
                break;
            case HeapType::DSV:
                // DSV ヒープを作成します（シェーダー非表示）
                create_descriptor_heap(heapType, dsvCap, false);

                // デプスステンシルのテーブルを初期化します
                m_depthStencils.m_heapType = heapType;
                m_depthStencils.m_capacity = dsvCap;
                m_depthStencils.m_baseIndex = 0;
                m_depthStencils.m_freeList.reserve(dsvCap);
                for (uint32_t j = 0; j < dsvCap; ++j)
                {
                    m_depthStencils.m_freeList.push_back(dsvCap - 1u - j);
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
    TableID DescriptorAllocator::allocate(TableKind k)
    {
        // 対象テーブルを取得して空き状況を確認する
        // 空きがあれば割り当て済みインデックスを返す
        Table& t = get_table(k);
        if (t.m_freeList.empty())
        {
            CUE_ASSERT_MSG(false, "Descriptor table full, need to expand.");
            return TableID{ k, t.m_generation, TableID::kInvalid };
        }

        // 空きがあるので割り当てる
        uint32_t idx = t.m_freeList.back();
        t.m_freeList.pop_back();
        return TableID{ k, t.m_generation, idx };
    }
    void DescriptorAllocator::free_table(TableID id)
    {
        // 無効 ID を弾いて再利用ミスを防ぐ
        // 空きリストに戻して再割り当て可能にする
        if (!id.valid())
        {
            return;
        }

        Table& t = get_table(id.m_kind);
        t.m_freeList.push_back(id.m_index);
    }
    D3D12_GPU_DESCRIPTOR_HANDLE DescriptorAllocator::get_table_base_gpu(TableKind k)
    {
        // テーブル情報を取得する
        Table& t = get_table(k);

        // ヒープ種別に応じて GPU ハンドルを返す
        D3D12_GPU_DESCRIPTOR_HANDLE baseHandle{};
        if (t.m_heapType == HeapType::CBV_SRV_UAV)
        {
            // CBV_SRV_UAV テーブルは GPU 可視ヒープからのオフセットで返す
            baseHandle = m_gpuSrvUavDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
        }
        else
        {
            // その他のテーブルは対応するヒープからのオフセットで返す
            baseHandle = m_descriptorHeaps[static_cast<size_t>(t.m_heapType)]->GetGPUDescriptorHandleForHeapStart();
        }

        // テーブルの先頭スロットのオフセットを加算して返す
        baseHandle.ptr += static_cast<SIZE_T>(m_descriptorSizes[static_cast<size_t>(t.m_heapType)]) * (t.m_baseIndex);

        return baseHandle;
    }
    D3D12_GPU_DESCRIPTOR_HANDLE DescriptorAllocator::get_gpu_handle(TableID id)
    {
        // ID の妥当性を確認する
        if (!id.valid())
        {
            return D3D12_GPU_DESCRIPTOR_HANDLE_NULL;
        }

        // テーブル情報を取得する
        Table& t = get_table(id.m_kind);

        // ヒープ種別に応じて GPU ハンドルを返す
        D3D12_GPU_DESCRIPTOR_HANDLE handle{};
        if (t.m_heapType == HeapType::CBV_SRV_UAV)
        {
            // CBV_SRV_UAV テーブルは GPU 可視ヒープからのオフセットで返す
            handle = m_gpuSrvUavDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
        }
        else
        {
            // その他のテーブルは対応するヒープからのオフセットで返す
            handle = m_descriptorHeaps[static_cast<size_t>(t.m_heapType)]->GetGPUDescriptorHandleForHeapStart();
        }

        // テーブルの先頭スロットのオフセットとテーブル内のインデックスのオフセットを加算して返す
        handle.ptr += static_cast<SIZE_T>(m_descriptorSizes[static_cast<size_t>(t.m_heapType)]) * (t.m_baseIndex + id.m_index);

        return handle;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE DescriptorAllocator::get_cpu_handle_gpu_visible(TableID id)
    {
        // ID の妥当性を確認する
        if (!id.valid())
        {
            return D3D12_CPU_DESCRIPTOR_HANDLE_NULL;
        }

        // テーブル情報を取得する
        Table& t = get_table(id.m_kind);

        // GPU 可視ヒープからのオフセットで CPU ハンドルを返す
        D3D12_CPU_DESCRIPTOR_HANDLE handle = m_gpuSrvUavDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(m_descriptorSizes[static_cast<size_t>(t.m_heapType)]) * (t.m_baseIndex + id.m_index);

        return handle;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE DescriptorAllocator::get_cpu_handle(TableID id)
    {
        // ID の妥当性を確認する
        if (!id.valid())
        {
            return D3D12_CPU_DESCRIPTOR_HANDLE_NULL;
        }

        // テーブル情報を取得する
        Table& t = get_table(id.m_kind);

        // ヒープ種別に応じて CPU ハンドルを返す
        D3D12_CPU_DESCRIPTOR_HANDLE handle{};
        handle = m_descriptorHeaps[static_cast<size_t>(t.m_heapType)]->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(m_descriptorSizes[static_cast<size_t>(t.m_heapType)]) * (t.m_baseIndex + id.m_index);

        return handle;
    }

    // --- heap 取得 ---
    ID3D12DescriptorHeap* DescriptorAllocator::get_descriptor_heap(HeapType type) const noexcept
    {
        // GPU 可視ヒープが必要な種別を先に判定する
        // 対応する CPU/CPU&GPU ヒープを返す
        if (type == HeapType::CBV_SRV_UAV)
        {
            // CBV_SRV_UAV は GPU 可視ヒープを返す
            return m_gpuSrvUavDescriptorHeap.Get();
        }
        return m_descriptorHeaps[static_cast<size_t>(type)].Get();
    }
    Result DescriptorAllocator::compute_descriptor_sizes()
    {
        // 各 descriptor heap タイプのディスクリプタサイズを取得して保存します
        for (size_t i = 0; i < static_cast<size_t>(HeapType::kCount); ++i)
        {
            HeapType heapType = static_cast<HeapType>(i);

            D3D12_DESCRIPTOR_HEAP_TYPE d3dHeapType = to_d3d12_heap_type(heapType);

            m_descriptorSizes[i] = m_device.GetDescriptorHandleIncrementSize(d3dHeapType);
        }

        return Result::ok();
    }
    Result DescriptorAllocator::create_descriptor_heap(HeapType heapType, uint32_t size, bool shader_visible)
    {
        D3D12_DESCRIPTOR_HEAP_TYPE d3dHeapType = to_d3d12_heap_type(heapType);
        D3D12_DESCRIPTOR_HEAP_DESC desc{};

        desc.Type = d3dHeapType;
        desc.NumDescriptors = size;
        if (shader_visible && heapType == HeapType::CBV_SRV_UAV)
        {
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            m_device.CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_gpuSrvUavDescriptorHeap));
            SetD3D12Name(m_gpuSrvUavDescriptorHeap.Get(), (L"GPU " + to_string(heapType) + L" Descriptor Heap").c_str());
        }
        else
        {
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            m_device.CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_descriptorHeaps[static_cast<size_t>(heapType)]));
            SetD3D12Name(m_descriptorHeaps[static_cast<size_t>(heapType)].Get(), (L"CPU " + to_string(heapType) + L" Descriptor Heap").c_str());
        }

        return Result::ok();
    }
    DescriptorAllocator::Table& DescriptorAllocator::get_table(TableKind k)
    {
        // 種類に応じてテーブルを返す
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
            CUE_ASSERT_MSG(false, "Invalid TableKind");
            break;
        }

        // 無効なテーブルを返す
        return m_textures; // どれでもいいので返す
    }
    void DescriptorAllocator::copy_to_gpu_heap(TableID id)
    {
        // ID の妥当性を確認する
        if (!id.valid())
        {
            return;
        }

        // テーブル情報を取得する
        Table& t = get_table(id.m_kind);

        // GPU 可視ヒープへコピーする（CBV_SRV_UAV テーブルのみ）
        if (t.m_heapType != HeapType::CBV_SRV_UAV)
        {
            return;
        }

        // コピー元とコピー先のハンドルを取得する
        D3D12_CPU_DESCRIPTOR_HANDLE srcHandle = get_cpu_handle(id);
        D3D12_CPU_DESCRIPTOR_HANDLE dst = m_gpuSrvUavDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

        // コピー先のオフセットを計算する
        const SIZE_T inc =
            static_cast<SIZE_T>(m_descriptorSizes[static_cast<size_t>(HeapType::CBV_SRV_UAV)]);
        const uint32_t slotIndex = t.m_baseIndex + id.m_index;

        dst.ptr += inc * slotIndex;

        // コピー
        m_device.CopyDescriptorsSimple(
            1,
            dst,        // コピー先 (GPU ヒープ)
            srcHandle,  // コピー元 (CPU ヒープ)
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
}
