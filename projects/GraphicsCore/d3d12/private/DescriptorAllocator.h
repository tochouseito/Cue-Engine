#pragma once
#include "stdafx.h"
#include "RenderDevice.h"
#include "GpuBuffer.h"

namespace Cue::GraphicsCore::DX12
{
    /// @brief ディスクリプタヒープの種類
    enum class HeapType : uint8_t
    {
        CBV_SRV_UAV,
        SAMPLER,
        RTV,
        DSV,
        kCount
    };

    class DescriptorAllocator final
    {
    public:
        DescriptorAllocator(RenderDevice& renderDevice)
            : m_renderDevice(renderDevice)
        {
        }
        ~DescriptorAllocator() = default;

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
            uint16_t  m_generation{};  ///< テーブルの世代（将来の再配置用）
            uint32_t  m_index = kInvalid;       ///< テーブル内のローカル index（0..capacity-1）

            static constexpr uint32_t kInvalid = 0xFFFFFFFF;

            bool valid() const { return m_index != kInvalid; }
        };

        // 初期化
        [[nodiscard]] Result initialize();

        // テーブル別の割り当て/解放
        [[nodiscard]] Result allocate(TableKind k);
        void free_table(TableID id);

        // 各種 view 作成
        void create_cbv(TableID& id, GpuBufferResource* buffer);
        void create_srv_buffer(TableID& id, GpuBufferResource* buffer);
        void create_uav_buffer(TableID& id, GpuBufferResource* buffer);
        void create_uav_raw_buffer(TableID& id, GpuBufferResource* buffer);

        void create_srv_texture_2d(TableID& id, GpuTextureResource* texture);
        void create_rtv(TableID& id, GpuTextureResource* texture);
        void create_dsv(TableID& id, GpuTextureResource* texture);

        // CPU/GPU デスクリプタハンドルの取得
        D3D12_GPU_DESCRIPTOR_HANDLE get_table_base_gpu(TableKind k);
        D3D12_GPU_DESCRIPTOR_HANDLE get_gpu_handle(TableID id);
        D3D12_CPU_DESCRIPTOR_HANDLE get_cpu_handle_gpu_visible(TableID id);
        D3D12_CPU_DESCRIPTOR_HANDLE get_cpu_handle(TableID id);

        // ディスクリプタヒープ取得
        ID3D12DescriptorHeap* get_descriptor_heap(HeapType type) const noexcept
        {
            // 1) GPU 可視ヒープが必要な種別を先に判定する
            // 2) 対応する CPU/CPU&GPU ヒープを返す
            if (type == HeapType::CBV_SRV_UAV)
            {
                // CBV_SRV_UAV は GPU 可視ヒープを返す
                return m_gpuSrvUavHeap.Get();
            }
            return m_descriptorHeaps[static_cast<size_t>(type)].Get();
        }
    private:
        // テーブル内部情報
        struct Table final
        {
            uint32_t m_baseIndex = 0;            ///< ヒープ内の先頭スロット
            uint32_t m_capacity = 0;
            uint16_t m_generation = 0;
            std::vector<uint32_t> m_freeList;    ///< 空きスロット
            HeapType m_heapType = HeapType::CBV_SRV_UAV;
        };
        /// @brief 旧ヒープの寿命管理（GPU 完了まで保持）※実装予定
        struct RetiredHeap
        {
            ComPtr<ID3D12DescriptorHeap> m_heap;
            uint64_t m_fence = 0;
        };
        // 足りなければ拡張（実装予定）
        void ensure_capacity(TableKind k, uint32_t needOneMore);

        // 再配置（実装予定）
        void recreate_heap(TableKind k, uint32_t newCap, uint32_t newBufCap = 0);

        // テーブル取得
        Table& get_table(TableKind k);

        // CBV_SRV_UAV テーブルのディスクリプタを GPU 可視ヒープへコピー
        void copy_to_gpu_heap(const TableID& id);

    private:
        RenderDevice& m_renderDevice;
        // ヒープタイプごとのディスクリプタサイズ
        std::array<UINT, static_cast<size_t>(HeapType::kCount)> m_descriptorSizes = { 0 };

        Table m_textures;
        Table m_buffers;
        Table m_renderTargets;
        Table m_depthStencils;
        std::vector<RetiredHeap> m_retired;

        // 各種ディスクリプタヒープ初期サイズ
        static constexpr uint32_t kMaxSrvUavDescriptorHeapSize = 1024;
        static constexpr uint32_t kMaxRtvDescriptorHeapSize = 32;
        static constexpr uint32_t kMaxDsvDescriptorHeapSize = 2;

        /// @brief CPU 専用ディスクリプタヒープ（タイプごと）
        std::array<ComPtr<ID3D12DescriptorHeap>, static_cast<size_t>(HeapType::kCount)> m_descriptorHeaps{};

        /// @brief CBV_SRV_UAV 用 GPU 可視ヒープ
        ComPtr<ID3D12DescriptorHeap> m_gpuSrvUavHeap = nullptr;
    };
} // namespace Cue::GraphicsCore::DX12
