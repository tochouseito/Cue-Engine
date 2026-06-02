// DescriptorAllocator の役割と公開要素を定義する

#pragma once

// === RHI includes ===
#include <RHICommon.h>

// === C++ includes ===
#include <array>

// === DirectX 12 includes ===
#include "stdafx.h"
#include "DX12GpuResource.h"

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
        TableKind kind = TableKind::Buffers;
        uint16_t  generation{};           ///< テーブルの世代（将来の再配置用）
        uint32_t  index = kInvalid;       ///< テーブル内のローカル index（0..capacity-1）

        static constexpr uint32_t kInvalid = 0xFFFFFFFF;

        bool valid() const { return index != kInvalid; }
    };

    class DescriptorAllocator final
    {
    public:
        explicit DescriptorAllocator(ID3D12Device& a_device) :
            m_device(a_device)
        {
        }

        // コピー禁止
        DescriptorAllocator(const DescriptorAllocator&) = delete;
        DescriptorAllocator& operator=(const DescriptorAllocator&) = delete;
        // ムーブ禁止
        DescriptorAllocator(DescriptorAllocator&&) = delete;
        DescriptorAllocator& operator=(DescriptorAllocator&&) = delete;
        ~DescriptorAllocator() = default;

        /// @brief デスクリプタヒープと各テーブルを初期化する
        Result initialize(uint32_t a_texCap, uint32_t a_bufCap, uint32_t a_rtvCap, uint32_t a_dsvCap);

        /// @brief 指定種類のテーブルスロットを割り当て
        [[nodiscard]] TableID allocate(TableKind a_kind);

        /// @brief 割り当て済みテーブルスロットを解放し
        void free_table(TableID a_id);

        /// @brief テーブル先頭の GPU ハンドルを取得する
        D3D12_GPU_DESCRIPTOR_HANDLE get_table_base_gpu(TableKind a_kind);

        /// @brief 指定テーブル ID の GPU ハンドルを取得する
        D3D12_GPU_DESCRIPTOR_HANDLE get_gpu_handle(TableID a_id);

        /// @brief GPU 可視ヒープ上の CPU ハンドルを取得する
        D3D12_CPU_DESCRIPTOR_HANDLE get_cpu_handle_gpu_visible(TableID a_id);

        /// @brief CPU ヒープ上のハンドルを取得する
        D3D12_CPU_DESCRIPTOR_HANDLE get_cpu_handle(TableID a_id);

        /// @brief shader-visible な texture descriptor を 1 つ割り当て
        [[nodiscard]] Result allocate_shader_visible_texture_descriptor(
            D3D12_CPU_DESCRIPTOR_HANDLE& a_outCpuHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE& a_outGpuHandle);

        /// @brief shader-visible な texture descriptor を解放し
        void free_shader_visible_texture_descriptor(
            D3D12_CPU_DESCRIPTOR_HANDLE a_cpuHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE a_gpuHandle);

        /// @brief 指定ヒープ種別に対応するヒープを取得する
        ID3D12DescriptorHeap* get_descriptor_heap(HeapType a_type) const noexcept;

        // --- View の作成 ---
        Result create_cbv(TableID id, DX12GpuResource* resource, uint64_t byteOffset, uint32_t byteSize);
        Result create_srv_buffer(TableID id, DX12GpuResource* resource, uint64_t firstElement, uint32_t numElements, uint32_t structureByteStride);
        Result create_srv_raw_buffer(TableID id, DX12GpuResource* resource, uint64_t firstElement, uint32_t numElements);
        Result create_uav_buffer(TableID id, DX12GpuResource* resource, uint64_t firstElement, uint32_t numElements, uint32_t structureByteStride);
        Result create_uav_raw_buffer(TableID id, DX12GpuResource* resource, uint64_t firstElement, uint32_t numElements);

        Result create_srv_texture_2d(TableID id, DX12GpuResource* resource, DXGI_FORMAT format, uint32_t mipSlice, uint32_t mipLevels);
        Result create_srv_texture_cube(TableID id, DX12GpuResource* resource, DXGI_FORMAT format, uint32_t mipSlice, uint32_t mipLevels);
        Result create_uav_texture_2d(TableID id, DX12GpuResource* resource, DXGI_FORMAT format, uint32_t mipSlice);
        Result create_rtv(TableID id, DX12GpuResource* resource, DXGI_FORMAT format, uint32_t mipSlice);
        Result create_rtv(TableID id, ID3D12Resource* resource, DXGI_FORMAT format);
        Result create_dsv(TableID id, DX12GpuResource* resource, DXGI_FORMAT format, uint32_t mipSlice);

    private:
        // --- DescriptorHeap 作成 ---
        Result compute_descriptor_sizes();
        Result create_descriptor_heap(HeapType a_heapType, uint32_t a_size, bool a_shaderVisible);

        // テーブル内部情報
        struct Table final
        {
            uint32_t baseIndex = 0;            ///< ヒープ内の先頭スロット
            uint32_t capacity = 0;
            uint16_t generation = 0;
            std::vector<uint32_t> freeList;    ///< 空きスロット
            HeapType heapType = HeapType::CBV_SRV_UAV;
        };

        // --- テーブル管理 ---
        Table& get_table(TableKind a_kind);

        // GPU ヒープへのコピー
        void copy_to_gpu_heap(TableID a_id);

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
        std::array<comPtr<ID3D12DescriptorHeap>, static_cast<size_t>(HeapType::kCount)> m_descriptorHeaps = { nullptr };
        // GPU 対応ヒープ
        comPtr<ID3D12DescriptorHeap> m_gpuSrvUavDescriptorHeap = nullptr;

        // --- テーブル管理 ---
        Table m_textures;
        Table m_buffers;
        Table m_renderTargets;
        Table m_depthStencils;
    };
}
