#pragma once

// === RHI include ===
#include <RHICommon.h>

// === C++ include ===
#include <array>

// === DirectX 12 include ===
#include "stdafx.h"

namespace Cue::RHI::DX12
{
    // デスクリプタヒープの種類
    enum class HeapType : uint8_t
    {
        CBV_SRV_UAV,
        SAMPLER,
        RTV,
        DSV,
        kCount
    };

    // ディスクリプタテーブルの種類
    enum class TableKind : uint8_t
    {
        Textures,
        Buffers,
        RenderTargets,
        DepthStencils
    };

    // テーブルID
    struct TableID final
    {
        TableKind m_kind = TableKind::Buffers;
        uint16_t  m_generation{};           ///< テーブルの世代（将来の再配置用）
        uint32_t  m_index = kInvalid;       ///< テーブル内のローカル index（0..capacity-1）

        static constexpr uint32_t kInvalid = 0xFFFFFFFF;

        bool valid() const { return m_index != kInvalid; }
    };

    class DescriptorAllocator final
    {
    public:
        DescriptorAllocator(ID3D12Device& device):
            m_device(device)
        {
        }
        // コピー禁止
        DescriptorAllocator(const DescriptorAllocator&) = delete;
        DescriptorAllocator& operator=(const DescriptorAllocator&) = delete;
        // ムーブ禁止
        DescriptorAllocator(DescriptorAllocator&&) = delete;
        DescriptorAllocator& operator=(DescriptorAllocator&&) = delete;
        ~DescriptorAllocator() = default;

        // 初期化
        Result initialize(uint32_t texCap, uint32_t bufCap, uint32_t rtvCap, uint32_t dsvCap);

        // --- 割り当て/解放 ---
        [[nodiscard]] TableID allocate(TableKind k);
        void free_table(TableID id);

        // --- handle 取得 ---
        D3D12_GPU_DESCRIPTOR_HANDLE get_table_base_gpu(TableKind k);
        D3D12_GPU_DESCRIPTOR_HANDLE get_gpu_handle(TableID id);
        D3D12_CPU_DESCRIPTOR_HANDLE get_cpu_handle_gpu_visible(TableID id);
        D3D12_CPU_DESCRIPTOR_HANDLE get_cpu_handle(TableID id);

        // --- heap 取得 ---
        ID3D12DescriptorHeap* get_descriptor_heap(HeapType type) const noexcept;
    private:
        // --- DescriptorHeap 作成 ---
        Result compute_descriptor_sizes();
        Result create_descriptor_heap(HeapType heapType, uint32_t size, bool shader_visible);

        // テーブル内部情報
        struct Table final
        {
            uint32_t m_baseIndex = 0;            ///< ヒープ内の先頭スロット
            uint32_t m_capacity = 0;
            uint16_t m_generation = 0;
            std::vector<uint32_t> m_freeList;    ///< 空きスロット
            HeapType m_heapType = HeapType::CBV_SRV_UAV;
        };

        // --- テーブル管理 ---
        Table& get_table(TableKind k);
        // GPU ヒープへのコピー
        void copy_to_gpu_heap(TableID id);
    private:
        ID3D12Device& m_device;

        // --- ヒープタイプごとのディスクリプタサイズ ---
        std::array<UINT, static_cast<size_t>(HeapType::kCount)> m_descriptorSizes = { 0 };

        // --- 各種ディスクリプタヒープ初期サイズ ---
        static constexpr uint32_t kMaxSrvUavDescriptorHeapSize = 1024;
        static constexpr uint32_t kMaxRtvDescriptorHeapSize = 32;
        static constexpr uint32_t kMaxDsvDescriptorHeapSize = 2;

        // --- デスクリプタヒープ ---
        // CPU 専用ヒープ
        std::array<ComPtr<ID3D12DescriptorHeap>, static_cast<size_t>(HeapType::kCount)> m_descriptorHeaps = { nullptr };
        // GPU 対応ヒープ
        ComPtr<ID3D12DescriptorHeap> m_gpuSrvUavDescriptorHeap = nullptr;

        // --- テーブル管理 ---
        Table m_textures;
        Table m_buffers;
        Table m_renderTargets;
        Table m_depthStencils;
    };
}
